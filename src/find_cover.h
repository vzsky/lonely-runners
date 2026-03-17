#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <iostream>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "speedset.h"
#include "utils.h"

// Step 1: Brute-force search for all k-tuples (mod p) whose union covers all
// unsafe times
namespace find_cover
{

// Precompute coverage bitsets for a given level n
template <int P> using CoverageArray = std::array<std::bitset<P / 2>, P / 2 + 1>;

template <int K, int P> CoverageArray<P> make_stationary_runner_coverage_mask()
{
  CoverageArray<P> arr{};
  for (int i = 0; i <= P / 2; ++i)
  {
    for (int t = 1; t <= P / 2; ++t)
    {
      int pos     = P / 2 - t;
      int rem     = (int)((1LL * t * i) % P);
      arr[i][pos] = (1LL * rem * (K + 1) < P) || (1LL * (P - rem) * (K + 1) < P);
    }
  }
  return arr;
}

template <int K, int P>
bool early_return_bound(const CoverageArray<P>& cov, const std::array<int, P / 2>& remaining,
                        const std::bitset<P / 2>& covered, const std::vector<char>& eliminated, int used,
                        int nextToCover)
{
  constexpr int bitlen   = P / 2;
  constexpr int maxIndex = P / 2;
  if (nextToCover != -1 && remaining[nextToCover] == 0) [[unlikely]]
    return true;

  if (used < K - 5 || nextToCover == -1) return false;

  int slots = K - used - 1;

  std::bitset nextC  = ~covered;
  nextC[nextToCover] = 0;

  int totalToCover = bitlen - (int)covered.count();

  std::vector<long long> contribs;
  contribs.reserve(maxIndex);

  long long bestCovering_next = 0;
  for (int i = 1; i <= maxIndex; ++i)
  {
    if (eliminated[i]) continue;
    long long c = (nextC & cov[i]).count();
    contribs.push_back(c);
    if (cov[i][nextToCover]) bestCovering_next = std::max(bestCovering_next, c + 1);
  }

  std::partial_sort(contribs.begin(), contribs.begin() + std::min((int)contribs.size(), slots),
                    contribs.end(), std::greater<>());

  long long topSum = 0;
  for (int i = 0; i < std::min((int)contribs.size(), slots); ++i) topSum += contribs[i];

  return totalToCover > topSum + bestCovering_next;
}

template <int K, int P> struct DfsSeed
{
  int depth;
  std::bitset<P / 2> covered;
  std::vector<char> eliminated;
  SpeedSet<K> elems;
  std::array<int, P / 2> remaining;
  int wasted_bits;
};

template <int K, int P> struct Dfs
{
  const CoverageArray<P>& cov;
  DfsSeed<K, P>& seed;
  SetOfSpeedSets<K> solutions{};

  void run(int depth, std::bitset<P / 2> current_covered, int wasted_bits)
  {
    constexpr int p            = P;
    constexpr int maxI         = P / 2;
    constexpr int bitlen       = P / 2;
    constexpr int bits_per_set = P / (K + 1);
    constexpr int max_waste    = K * bits_per_set - bitlen;

    if (depth == K)
    {
      if (current_covered.count() != bitlen) return;
      solutions.insert(seed.elems);
      return;
    }

    int nextToCover = -1, best = INT_MAX;
    for (int pos = 0; pos < bitlen; ++pos)
      if (!current_covered[pos] && seed.remaining[pos] < best)
      {
        best        = seed.remaining[pos];
        nextToCover = pos;
      }

    if (wasted_bits > max_waste) return;
    if (early_return_bound<K, P>(cov, seed.remaining, current_covered, seed.eliminated, depth, nextToCover))
    {
      return;
    }

    const std::array saved_remaining         = seed.remaining;
    const std::vector<char> saved_eliminated = seed.eliminated;
    for (int i = 1; i <= maxI; ++i)
    {
      if (seed.eliminated[i]) continue;
      if (nextToCover == -1 || cov[i][nextToCover])
      {
        seed.elems.insert(i);
        int overlap              = (int)(current_covered & cov[i]).count();
        std::bitset next_covered = current_covered;
        next_covered |= cov[i];
        run(depth + 1, std::move(next_covered), wasted_bits + overlap);
        seed.elems.remove(i);
        seed.eliminated[i] = 1;
        for (int pos = 0; pos < bitlen; ++pos)
          if (cov[i][pos]) seed.remaining[pos]--;
      }
    }
    seed.remaining  = saved_remaining;
    seed.eliminated = saved_eliminated;
  }
};

template <int K, int P> static SetOfSpeedSets<K> run_dfs(const CoverageArray<P>& cov, DfsSeed<K, P>&& seed)
{
  auto d = Dfs<K, P>{cov, seed};
  d.run(seed.depth, seed.covered, seed.wasted_bits);
  return d.solutions;
}

template <int K, int P> static SetOfSpeedSets<K> find_all_covers_parallel(const CoverageArray<P>& cov)
{
  std::array<int, P / 2> remaining0{};
  for (int i = 1; i <= P / 2; ++i)
    for (int pos = 0; pos < P / 2; ++pos)
      if (cov[i][pos]) remaining0[pos]++;

  // check feasibility
  for (int pos = 0; pos < P / 2; ++pos)
    if (remaining0[pos] == 0) return {};

  // Fix element 1 as first pick
  std::vector<char> base_eliminated(P / 2 + 1, 0);
  std::array<int, P / 2> base_remaining = remaining0;
  base_eliminated[1]                    = 1;
  for (int pos = 0; pos < P / 2; ++pos)
    if (cov[1][pos]) base_remaining[pos]--;

  std::bitset<P / 2> first_covered = cov[1];
  SpeedSet<K> elems{};
  elems.insert(1);

  // Collect second-level candidates for parallelism
  int nextToCover1 = -1, best1 = INT_MAX;
  for (int pos = 0; pos < P / 2; ++pos)
    if (!first_covered[pos] && base_remaining[pos] < best1)
    {
      best1        = base_remaining[pos];
      nextToCover1 = pos;
    }

  std::vector<int> top_candidates;
  for (int i = 2; i <= P / 2; ++i) // start from 2 since 1 is fixed
    if (nextToCover1 == -1 || cov[i][nextToCover1]) top_candidates.push_back(i);

  size_t nthreads = parallelize_core();
  if (nthreads > top_candidates.size()) nthreads = top_candidates.size();

  std::vector<SetOfSpeedSets<K>> thread_results(nthreads);
  std::vector<std::thread> threads;
  size_t chunk = (top_candidates.size() + nthreads - 1) / nthreads;

  for (unsigned int t = 0; t < nthreads; ++t)
  {
    size_t lo = t * chunk;
    size_t hi = std::min(lo + chunk, top_candidates.size());
    if (lo >= hi) break;

    threads.emplace_back([&, lo, hi, t]
    {
      std::vector<char> elim     = base_eliminated;
      std::array<int, P / 2> rem = base_remaining;

      for (size_t idx = 0; idx < lo; ++idx)
      {
        int j   = top_candidates[idx];
        elim[j] = 1;
        for (int pos = 0; pos < P / 2; ++pos)
          if (cov[j][pos]) rem[pos]--;
      }

      for (size_t idx = lo; idx < hi; ++idx)
      {
        int i = top_candidates[idx];

        int wasted_bits            = (first_covered & cov[i]).count();
        std::bitset<P / 2> covered = first_covered | cov[i];
        SpeedSet<K> local_elems    = elems;
        local_elems.insert(i);

        thread_results[t].merge(
            run_dfs<K, P>(cov, {2, std::move(covered), elim, local_elems, rem, wasted_bits}));

        elim[i] = 1;
        for (int pos = 0; pos < P / 2; ++pos)
          if (cov[i][pos]) rem[pos]--;
      }
    });
  }

  for (auto& th : threads) th.join();

  SetOfSpeedSets<K> base_solutions;
  for (unsigned int t = 0; t < nthreads; ++t) base_solutions.merge(thread_results[t]);

  return base_solutions;
}

} // namespace find_cover
