#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <iostream>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "speedset.h"
#include "utils.h"

namespace lift
{

// todo: get rid of this struct
struct WordBitset
{
private:
  using u64 = uint64_t;
  int nbits{}, nwords{};

  std::vector<u64> w;

public:
  WordBitset() = default;
  explicit WordBitset(int bits) { reset(bits); }

  void reset(int bits)
  {
    nbits  = bits;
    nwords = (bits + 63) >> 6;
    w.assign(nwords, 0ULL);
  }

  inline void setBit(int pos) { w[pos >> 6] |= (1ULL << (pos & 63)); }

  inline bool testBit(int pos) const { return ((w[pos >> 6] >> (pos & 63)) & 1ULL) != 0; }

  inline long long count() const
  {
    long long s = 0;
    for (u64 x : w) s += __builtin_popcountll(x);
    return s;
  }

  inline void orWith(const WordBitset& o)
  {
    int m = std::min(nwords, o.nwords);
    for (int i = 0; i < m; ++i) w[i] |= o.w[i];
  }

  inline void clearBit(int pos) { w[pos >> 6] &= ~(1ULL << (pos & 63)); }

  WordBitset complement() const
  {
    WordBitset result(nbits);
    for (int i = 0; i < nwords; ++i) result.w[i] = ~w[i];
    int excess = nwords * 64 - nbits;
    if (excess > 0) result.w[nwords - 1] &= (~u64(0)) >> excess;
    return result;
  }

  long long andCount(const WordBitset& o) const
  {
    int m       = std::min(nwords, o.nwords);
    long long s = 0;
    for (int i = 0; i < m; ++i) s += __builtin_popcountll(w[i] & o.w[i]);
    return s;
  }
};

// Context struct encapsulates the lifting level (mod Q = np) and bitset
// precomputations
// todo: get rid of this struct
struct Context
{
  int p{}, n{}, Q{};
  int maxIndex{}, bitlen{}, nwords{};
  // For each index i, vec[i] is the bitset for coverage testing
  std::vector<WordBitset> vec;
};

Context make_context(int p, int k, int n, bool fullRange)
{
  Context C{};
  C.p        = p;
  C.n        = n;
  C.Q        = n * p;
  C.maxIndex = fullRange ? C.Q - 1 : C.Q / 2;
  C.bitlen   = fullRange ? C.Q : C.Q / 2;
  C.vec.resize(C.maxIndex + 1, WordBitset(C.bitlen));

  // For each i in [0, Q) compute modular coverage bitset
  for (int i = 0; i <= C.maxIndex; ++i)
  {
    WordBitset& B = C.vec[i];
    for (int t = 1; t <= C.bitlen; ++t)
    {
      int pos = C.bitlen - t;
      int rem = (int)((1LL * t * i) % C.Q);
      // Check if i is not lonely at time t/Q (mod 1); condition per LRC definition
      bool cond = (1LL * rem * (k + 1) < C.Q) || (1LL * (C.Q - rem) * (k + 1) < C.Q);
      if (cond) B.setBit(pos);
    }
  }
  return C;
}

// Step 2 & 3: Lifting seeds from prior level to next level (n -> m*n)
// This function performs parallel lifting over the seed list and applies
// subset-GCD sieve and coverage test

template <int K> struct Dfs
{
  const Context& C;
  SpeedSet<K> elem;
  std::array<int, K> order;
  std::array<std::vector<int>, K> cand;
  SetOfSpeedSets<K> result;

  Dfs(const Context& ctx, const std::array<int, K>& ord, const std::array<std::vector<int>, K>& cnd)
      : C{ctx}, order{ord}, cand{cnd}
  {
  }

  void run(int depth)
  {
    if (depth == K)
    {
      WordBitset acc(C.bitlen);
      for (auto v : elem) acc.orWith(C.vec[v]);
      if (acc.count() != C.bitlen) return;

      if (elem.subset_gcd_implies_proper(C.n)) return;

      result.insert(elem.get_sorted_set());
      return;
    }
    int pos = order[depth];
    for (int candidate : cand[pos])
    {
      elem.insert(candidate);
      run(depth + 1);
      elem.remove(candidate);
    }
  }
};

template <int K> SetOfSpeedSets<K> lift(const Context& C, const SpeedSet<K>& seed, int multiplier)
{
  auto cand = [&] { // "superposition/shadow" of all candidate speedsets
    std::array<std::vector<int>, K> cand{};
    int j = 0;
    for (const auto &s : seed) {
      for (int a = 0; a < multiplier; a++) {
        long long val = (long long)s + (long long)a * (C.Q / multiplier);
        if (val <= C.maxIndex)
          cand[j].push_back((int)val);
        else
          break;
      }
      ++j;
    }
    return cand;
  }();

  std::array<int, K> order;
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int A, int B) { return cand[A].size() < cand[B].size(); });

  Dfs<K> runner{C, order, cand};
  runner.run(0);

  return runner.result;
};

template <int K>
SetOfSpeedSets<K> find_lifted_covers_parallel(const Context& C, const SetOfSpeedSets<K>& seeds,
                                              int multiplier)
{
  size_t N = seeds.size();
  if (N == 0) return {};

  unsigned int nthreads = parallelize_core();
  if (nthreads > N) nthreads = (unsigned int)N;

  std::vector<SetOfSpeedSets<K>> thread_results(nthreads);
  std::vector<std::thread> threads;

  auto worker = [&](auto begin, auto end, unsigned tid)
  {
    auto& local_results = thread_results[tid];

    for (auto it = begin; it != end; ++it)
    {
      local_results.merge(lift(C, *it, multiplier));
    }
  };

  // partition seeds
  size_t chunk = (N + nthreads - 1) / nthreads;
  auto it      = seeds.begin();

  for (unsigned int t = 0; t < nthreads && it != seeds.end(); ++t)
  {
    auto start  = it;
    size_t step = std::min(chunk, (size_t)std::distance(it, seeds.end()));
    std::advance(it, step);
    auto end = it;

    threads.emplace_back(worker, start, end, t);
  }

  for (auto& th : threads) th.join();

  // merge thread results into single std::vector, dedup across threads using
  // global set
  SetOfSpeedSets<K> results;
  for (unsigned int t = 0; t < nthreads; ++t) results.merge(thread_results[t]);
  return results;
}

} // namespace lift
