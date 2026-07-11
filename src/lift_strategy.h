#pragma once

#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>
#include <tuple>
#include <utility>

#include "lift.h"
#include "speedset.h"

// ============================================================================
// State
// ============================================================================

template <int LiftLevel, int P, int K> struct State
{
  SetOfSpeedSets<K> ansatz;
};

// ============================================================================
// Strategies
// ============================================================================

template <int Arg> struct Force
{
  friend std::ostream& operator<<(std::ostream& os, const Force&) { return os << "Force " << Arg; }

  template <int L, int P, int K> State<L * Arg, P, K> operator()(State<L, P, K> st) const
  {
    auto T = lift::find_lifted_covers_parallel<Arg, L, P, K>(st.ansatz);
    Log(std::format("Forcing Lift c={}: T size = {}", Arg, T.size()));
    return {std::move(T)};
  }
};

struct Project
{
  friend std::ostream& operator<<(std::ostream& os, const Project&) { return os << "Project"; }

  template <int L, int P, int K> State<1, P, K> operator()(State<L, P, K> st) const
  {
    SetOfSpeedSets<K> T;
    for (auto s : st.ansatz) T.insert(s.project(P).get_sorted_set());
    Log("Projected down");
    return {std::move(T)};
  }
};

template <int Limit = 0> struct Print
{
  friend std::ostream& operator<<(std::ostream& os, const Print&) { return os << "Print"; }

  template <int L, int P, int K> State<L, P, K> operator()(State<L, P, K> st) const
  {
    Log(std::format("ansatz: K={}, P={}, L={}", K, P, L));
    if (st.ansatz.size() == 0)
      Log("seeds: empty");
    else if (Limit == 0 || st.ansatz.size() <= Limit)
      Log("seeds: ", st.ansatz);
    else
      Log("seeds: too many");
    return st;
  }
};

struct TightLargePrime
{
  friend std::ostream& operator<<(std::ostream& os, const TightLargePrime&)
  {
    return os << "Remove (1..k) for large prime";
  }

  template <int L, int P, int K> State<L, P, K> operator()(State<L, P, K> st) const
  {
    if (P < K * (K + 1))
    {
      Log("Prime too small -- (1..k) not removed");
      return st;
    }

    st.ansatz.erase([]
    {
      SpeedSet<K> onetok{};
      for (int i = 1; i <= K; i++) onetok.insert(i);
      return onetok;
    }());

    return st;
  }
};

template <int Arg, int MaxIter = 3> struct Squeeze
{
  friend std::ostream& operator<<(std::ostream& os, const Squeeze&) { return os << "Squeeze"; }

private:
  template <int Remaining, int CurL, int P, int K>
  static State<1, P, K> iterate(State<CurL, P, K> lifted, State<1, P, K> last)
  {
    if (last.ansatz.empty()) return last;
    State<1, P, K> projected = Project{}(lifted);
    if (projected.ansatz.size() == last.ansatz.size()) return last;

    if constexpr (Remaining != 0)
      return iterate<Remaining - 1>(Force<Arg>{}(lifted), std::move(projected));
    else
      return projected;
  }

public:
  template <int L, int P, int K> State<1, P, K> operator()(State<L, P, K> st) const
  {
    static_assert(MaxIter > 0);
    return iterate<MaxIter - 1>(Force<Arg>{}(st), std::move(st));
  }
};

// ============================================================================
// Tuple visitor: 
// tuples of functions represent functions composition
// ============================================================================

template <typename Tuple, std::size_t I = 0, int L, int P, int K> auto apply_config(State<L, P, K>&& st)
{
  if constexpr (I < std::tuple_size_v<Tuple>)
  {
    const bool shouldLog = st.ansatz.size() != 0;
    if (shouldLog) Log(std::format("Step 2.{}", I));

    auto result = timeit([&]
    {
      PushLogScope(std::format("2.{}", I));
      auto config = std::tuple_element_t<I, Tuple>{};
      auto r      = config(st);
      if (shouldLog) Log(config, " :: Done :: ", "S.size() =", r.ansatz.size());
      return r;
    });
    return apply_config<Tuple, I + 1>(std::move(result));
  }
  else
    return st;
}
