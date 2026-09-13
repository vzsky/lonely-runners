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
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "bitset.h"
#include "inlined_vector.h"
#include "speedset.h"
#include "utils.h"

namespace find_cover
{

template <typename T, std::size_t K> class TopElements
{
  std::array<T,K> buf{};

public:
  void push(const T& v)
  {
    for (std::size_t i = 0; i < K; ++i)
    {
      if (v > buf[i])
      {
        for (std::size_t z = K - 1; z > i; --z) buf[z] = buf[z - 1];
        buf[i] = v;
        break;
      }
    }
  }

  T sum() const
  {
    T s{};
    for (std::size_t i = 0; i < K; ++i) s += buf[i];
    return s;
  }

  T min() const { return buf[K - 1]; }
};

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

    // TODO: closed form able?
    mClassSize = [&]
    {
      // every speed covers the same number of time
      auto c = mCover[0].count();
      for (int i = 1; i < P / 2; ++i) assert(mCover[i].count() == c);
      return c;
    }();
  }

  const CoveredBitset& cover(int i) const { return mCover[i]; }
  const CoveredBitset& cand(int pos) const { return mCand[pos]; }
  int class_size() const { return mClassSize; }

private:
  // NB. ideally this is const after initialization but compile time heavy enough
  CovArray mCover{};
  CovArray mCand{};
  int mClassSize = 0;
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
  };

  State state;

  SetOfSpeedSets<K> solutions{};

  void run()
  {
    const std::optional<int> nextToCover = state.choice.get_next_to_cover(state.covered);
    if (!nextToCover)
    {
      // already covered, the remaining slots are free!
      solutions.merge(state.elems.fill_free_slots(P));
      return;
    }

    if (K - state.elems.size() == 2)
    {
      finish_last_two(*nextToCover);
      return;
    }

    const auto children = next_choices(state, *nextToCover);

    const auto saved_choice = state.choice;

    for (int i : children)
    {
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
  void finish_last_two(int nextToCover)
  {
    const auto firstPicks   = next_choices(state, nextToCover);
    const auto saved_choice = state.choice;

    for (int i : firstPicks)
    {
      state.elems.insert(i + 1);
      const CoveredBitset stillUncovered = ~(state.covered | context<P, K>.cover(i));

      if (stillUncovered.any())
      {
        auto secondCandidates = ~CoveredBitset{};
        stillUncovered.for_each([&](int t) { secondCandidates &= context<P, K>.cand(t); });
        secondCandidates &= state.choice.available();

        secondCandidates.for_each([&](int j)
        {
          state.elems.insert(j + 1);
          solutions.insert(state.elems.get_canonical_representation(P));
          state.elems.remove(j + 1);
        });
      }
      else
      {
        solutions.merge(state.elems.fill_free_slots(P));
      }

      state.elems.remove(i + 1);
      state.choice.eliminate(i);
    }

    state.choice = saved_choice;
  }

  [[nodiscard]] static InlinedVector<int, P / 2> next_choices(State st, int nextToCover)
  {
    struct ScoredChild
    {
      int gain, index;
    };

    if (!st.choice.canBeCovered(nextToCover)) return {};

    const int need  = bitlen - st.covered.count();
    const int slots = K - st.elems.size();

    if (need > slots * context<P, K>.class_size()) return {};

    const auto avail        = st.choice.available();
    const CoveredBitset unc = ~st.covered;

    InlinedVector<ScoredChild, P / 2> children;
    long long total = 0;
    long long sum;
    int mn;
    const auto ok = utils::dispatch<K + 1>(slots, [&](auto S)
    {
      TopElements<int, S> top;
      avail.for_each([&](int v)
      {
        int g = (context<P, K>.cover(v) & unc).count();
        children.push_back({g, v});
        total += g;
        top.push(g);
      });
      if (children.size() < slots || total < need) return false;
      sum = top.sum();
      mn  = top.min();
      return true;
    });

    if (!ok || sum < need) return {};

    InlinedVector<int, P / 2> result;
    const int first_ele_need = need - sum + mn;
    for (const auto& child : children)
    {
      if (child.gain < first_ele_need) continue;
      if (!context<P, K>.cand(nextToCover).test(child.index)) continue;
      result.push_back(child.index);
    }
    return result;
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

  std::optional<int> nextToCover1 = base_choice.get_next_to_cover(first_covered);

  const std::vector<int> coord2_candidates = [&]
  { // all possible second coordinate
    std::vector<int> v;
    for (int i = 0; i < P / 2; ++i)
      if (!nextToCover1 || context<P, K>.cover(i)[*nextToCover1]) v.push_back(i);
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
    for (int i = 0; i < P / 2; ++i) context<P, K>.cover(i).for_each([&](int pos) { _remaining[pos]++; });
  }

  bool isEliminated(size_t i) const { return _eliminated.test(i); }
  bool canBeCovered(size_t i) const { return _remaining[i] != 0; }
  ElimArray available() const { return ~_eliminated; }

  // Bit position that should be covered next, or nullopt once every
  // position is already covered.
  std::optional<int> get_next_to_cover(CoveredBitset current_covered) const
  {
    std::optional<int> nextToCover;
    int best = std::numeric_limits<int>::max();

    const auto unc = ~current_covered;
    unc.for_each([&](int pos)
    {
      if (_remaining[pos] < best)
      {
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
