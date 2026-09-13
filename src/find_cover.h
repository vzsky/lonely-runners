#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "bitset.h"
#include "inlined_vector.h"
#include "speedset.h"
#include "utils.h"

namespace find_cover
{

template <int P, int K> struct Context
{
  using CoveredBitset = Bitset<P / 2>;
  using CovArray      = std::array<CoveredBitset, P / 2>;

  Context()
  {
    for (int i = 0; i < P / 2; ++i)
      for (int t = 1; t <= P / 2; ++t)
      {
        int pos = P / 2 - t;
        int rem = (1LL * t * (i + 1)) % P;
        if ((rem * (K + 1) < P) || ((P - rem) * (K + 1) < P))
        {
          mCover[i].set(pos);
          mCand[pos].set(i);
        }
      }
  }

  const CoveredBitset& cover(int i) const { return mCover[i]; }
  const CoveredBitset& cand(int pos) const { return mCand[pos]; }

private:
  // NB. ideally this is const after initialization but compile time heavy enough
  CovArray mCover{};
  CovArray mCand{};
};

template <int P, int K> static const Context<P, K> context{};

template <int P, int K> struct Dfs
{
  static constexpr int bitlen = P / 2;
  using CoveredBitset         = typename Context<P, K>::CoveredBitset;

  struct State
  {
    struct AvailableChoice;

    CoveredBitset covered;  // time covered so far
    SpeedSet<K> elems;      // elements chosen
    AvailableChoice choice; // available choice we can choose

  } state;

  SetOfSpeedSets<K> solutions{};

  void run()
  {
    if (state.elems.size() == K)
    {
      if (state.covered.count() != bitlen) return;
      solutions.insert(state.elems.get_canonical_representation(P));
      return;
    }

    if (early_return_bound()) return;

    const auto saved_choice = state.choice;

    const int nextToCover = state.choice.get_next_to_cover(state.covered);
    const auto avail   = state.choice.available();
    const auto choices = (nextToCover == -1) ? avail : (context<P, K>.cand(nextToCover) & avail);

    const CoveredBitset unc = ~state.covered;
    struct ScoredChild
    {
      int gain, index;
      // sort by higher gain first
      bool operator < (const ScoredChild& O) const 
      {
        return gain != O.gain ? gain > O.gain : index < O.index;
      }
    };
    InlinedVector<ScoredChild, P / 2> children;
    choices.for_each([&](int ind) { children.emplace_back((context<P, K>.cover(ind) & unc).count(), ind); });
    std::sort(children.begin(), children.end());

    for (auto & child : children)
    {
      int i = child.index;
      state.elems.insert(i + 1);
      CoveredBitset mem = state.covered;
      state.covered |= context<P, K>.cover(i);

      run();

      state.elems.remove(i + 1);
      state.choice.eliminate(i);
      state.covered = mem;
    }

    state.choice = saved_choice;
  }

private:
  bool early_return_bound() const
  {
    const int nextToCover = state.choice.get_next_to_cover(state.covered);
    if (nextToCover != -1 && !state.choice.canBeCovered(nextToCover)) return true;
    if (state.elems.size() < K - 4 || nextToCover == -1) return false; // TODO: K - 4 is arbitrary

    CoveredBitset nextC = ~state.covered;
    nextC[nextToCover]  = 0;

    const int totalToCover = bitlen - state.covered.count();

    int bestCovering = 0;
    const auto avail = state.choice.available();
    avail.for_each([&](int i) { bestCovering = std::max(bestCovering, (nextC & context<P, K>.cover(i)).count()); });

    int bestCovering_next = 0;
    (context<P, K>.cand(nextToCover) & avail).for_each([&](int i)
    { bestCovering_next = std::max(bestCovering_next, (nextC & context<P, K>.cover(i)).count() + 1); });

    const int slots = K - state.elems.size();
    return totalToCover > bestCovering_next + bestCovering * (slots - 1);
  }
};

template <int P, int K> static SetOfSpeedSets<K> find_all_covers_parallel()
{
  // Fix first coordinate to 1 and generate all second coordinate per worker thread.
  using CoveredBitset = typename Dfs<P, K>::CoveredBitset;

  typename Dfs<P, K>::State::AvailableChoice base_choice;
  CoveredBitset first_covered;
  SpeedSet<K> elems{};
  elems.insert(1);
  first_covered |= context<P, K>.cover(0);

  int nextToCover1 = base_choice.get_next_to_cover(first_covered);

  const std::vector<int> coord2_candidates = [&]
  { // all possible second coordinate
    std::vector<int> v;
    for (int i = 0; i < P / 2; ++i)
      if (nextToCover1 == -1 || context<P, K>.cover(i)[nextToCover1]) v.push_back(i);
    return v;
  }();
  const size_t ncands = coord2_candidates.size();
  const std::vector choices = [&]
  { // precompute the choices after using each candidate
    std::vector<typename Dfs<P, K>::State::AvailableChoice> v(ncands + 1);
    v[0] = base_choice;
    for (size_t idx = 0; idx < ncands; ++idx)
    {
      v[idx + 1] = v[idx];
      v[idx + 1].eliminate(coord2_candidates[idx]);
    }
    return v;
  }();

  const size_t nthreads = std::min(parallelize_core(), ncands);

  std::atomic<size_t> next_idx{0};
  std::vector<SetOfSpeedSets<K>> thread_results(nthreads);
  std::vector<std::thread> threads;

  Log("Spawning", nthreads, "threads for", ncands, "DfsStates");
  for (size_t t = 0; t < nthreads; ++t)
  {
    threads.emplace_back([&, t]
    {
      while (true)
      {
        size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
        if (idx >= ncands) break;

        int i = coord2_candidates[idx];

        SpeedSet<K> local_elems = elems;
        local_elems.insert(i + 1);

        Dfs<P, K> d(
            typename Dfs<P, K>::State{first_covered | context<P, K>.cover(i), local_elems, choices[idx]});
        d.run();
        thread_results[t].merge(d.solutions);
      }
    });
  }

  for (auto& th : threads) th.join();

  SetOfSpeedSets<K> base_solutions;
  for (size_t t = 0; t < nthreads; ++t) base_solutions.merge(thread_results[t]);

  return base_solutions;
}

template <int P, int K> struct Dfs<P, K>::State::AvailableChoice
{
private:
  using ElimArray   = Bitset<P / 2>;
  using RemainArray = std::array<char, P / 2>;

  ElimArray _eliminated{};  // bool for each choice
  RemainArray _remaining{}; // count of active choice that cover position i
public:
  AvailableChoice()
  {
    for (int i = 0; i < P / 2; ++i)
      context<P, K>.cover(i).for_each([&](int pos) { _remaining[pos]++; });
  }

  bool isEliminated(size_t i) const { return _eliminated.test(i); }
  bool canBeCovered(size_t i) const { return _remaining[i] != 0; }
  ElimArray available() const { return ~_eliminated; }

  // return bit position that should be covered next
  int get_next_to_cover(CoveredBitset current_covered) const
  {
    int nextToCover = -1, best = std::numeric_limits<int>::max();

    (~current_covered).for_each([&](int pos){
      if (_remaining[pos] < best) {
        best        = _remaining[pos];
        nextToCover = pos;
      }
    });

    return nextToCover;
  }

  void eliminate(int i)
  {
    _eliminated.set(i);
    context<P, K>.cover(i).for_each([&](int pos) { _remaining[pos]--; });
  }
};

} // namespace find_cover
