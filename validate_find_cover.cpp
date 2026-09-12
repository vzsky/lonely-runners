// Regression harness for the find_cover.h optimization port
// (improvement_plan.md). Exercises ONLY find_cover::find_all_covers_parallel
// (Stage 1 -- I(K,P,1)); lift.h/lift_strategy.h are never included, so this
// never runs Stage 2.
//
// For each (K,P) test case fixed below (the regression set from
// improvement_plan.md's "Per-item workflow": K=10 -> 127,199,461; K=11 ->
// 131,199; K=12 -> 139,199,211), prints K, P, solution count, and a
// deterministic sorted dump of every solution (space-separated speeds per
// line), so a before/after run can be diffed for byte-identical output.
// The larger prime per K (461 for K=10, 211 for K=12) was chosen by
// probing candidates from LrcVerifier<K>'s prime list to land close to
// ~2 minutes on the unoptimized baseline (see improvement_implementation.md
// for the probe results); K=11 was left at its original two cases.
//
// Build:  clang++ -std=c++23 -march=native -O3 -I. validate_find_cover.cpp -o validate_find_cover
// Run:    ./validate_find_cover > out.txt   (takes ~5 min total on the fixed set, unoptimized baseline)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "src/find_cover.h"

template <int P, int K> void run_case()
{
  using namespace std::chrono;
  auto t0 = high_resolution_clock::now();
  auto sols = find_cover::find_all_covers_parallel<P, K>();
  auto t1 = high_resolution_clock::now();
  double secs = duration_cast<microseconds>(t1 - t0).count() / 1e6;

  std::vector<SpeedSet<K>> v(sols.begin(), sols.end());
  std::sort(v.begin(), v.end(), [](const SpeedSet<K>& a, const SpeedSet<K>& b) {
    for (int i = 0; i < K; ++i)
    {
      int av = *(a.begin() + i), bv = *(b.begin() + i);
      if (av != bv) return av < bv;
    }
    return false;
  });

  printf("CASE K=%d P=%d count=%zu time=%.6fs\n", K, P, v.size(), secs);
  for (auto& s : v)
  {
    bool first = true;
    for (int x : s)
    {
      if (!first) putchar(' ');
      first = false;
      printf("%d", x);
    }
    putchar('\n');
  }
  printf("END_CASE K=%d P=%d\n", K, P);
}

int main()
{
  run_case<127, 10>();
  run_case<199, 10>();
  run_case<461, 10>();
  run_case<131, 11>();
  run_case<199, 11>();
  run_case<139, 12>();
  run_case<199, 12>();
  run_case<211, 12>();
}
