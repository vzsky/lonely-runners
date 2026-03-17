#include <iostream>
#include <array>
#include <vector>

#include "src/find_cover.h"
#include "src/lift.h"

template <int K, int P, std::array multipliers>
bool run_lift_test(const std::vector<int>& expected_results)
{
  try
  {
    constexpr int M = multipliers.size();

    if (expected_results.empty())
    {
      std::cerr << "  ✗ K=" << K << ", P=" << P
                << " - expected_results is empty, please provide expected values" << std::endl;
      return false;
    }

    if (expected_results.size() != M + 1)
    {
      std::cerr << "  ✗ K=" << K << ", P=" << P
                << " - expected_results size mismatch: expected " << (M + 1)
                << " elements, got " << expected_results.size() << std::endl;
      return false;
    }

    // Step 1: Create initial covers
    auto cov   = find_cover::make_stationary_runner_coverage_mask<K, P>();
    auto seeds = find_cover::find_all_covers_parallel<K, P>(cov);

    int seeds_count = (int)seeds.size();
    std::vector<int> results_sequence = {seeds_count};

    // Step 2+: Apply each multiplier, accumulating current_n
    auto current  = seeds;
    int current_n = 1;

    for (std::size_t m = 0; m < M; ++m)
    {
      int mult          = multipliers[m];
      lift::Context ctx = lift::make_context(P, K, current_n * mult, true);
      auto T            = lift::find_lifted_covers_parallel(ctx, current, mult);

      results_sequence.push_back((int)T.size());

      current    = T;
      current_n *= mult;
    }

    // Validate each step
    for (size_t i = 0; i < results_sequence.size(); ++i)
    {
      if (results_sequence[i] != expected_results[i])
      {
        std::cerr << "  ✗ K=" << K << ", P=" << P
                  << " - Mismatch at index " << i
                  << ": expected " << expected_results[i]
                  << " but got " << results_sequence[i] << std::endl;
        return false;
      }
    }

    std::cout << "  ✓ K=" << K << ", P=" << P << " - Sequence: [";
    for (size_t i = 0; i < results_sequence.size(); ++i)
    {
      if (i > 0) std::cout << ", ";
      std::cout << results_sequence[i];
    }
    std::cout << "]" << std::endl;
    return true;
  }
  catch (const std::exception& e)
  {
    std::cerr << "  ✗ K=" << K << ", P=" << P
              << " - Exception: " << e.what() << std::endl;
    return false;
  }
}

int main()
{
  int test_count = 0;
  int pass_count = 0;

  std::cout << "=== Lonely Runners Unit Tests ===" << std::endl << std::endl;
  std::cout << "Expected result vector format: [initial_seeds, after_mult1, after_mult2, ...]" << std::endl << std::endl;
  std::cout << "--- Tests ---" << std::endl;

  {
    std::cout << "Test 1: K=3, P=53, mults=[2,3]" << std::endl;
    constexpr std::array mults = {2, 3};
    if (run_lift_test<3, 53, mults>(std::vector<int>{3, 3, 6})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 2: K=3, P=67, mults=[2]" << std::endl;
    constexpr std::array mults = {2};
    if (run_lift_test<3, 67, mults>(std::vector<int>{3, 3})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 3: K=4, P=71, mults=[2,3]" << std::endl;
    constexpr std::array mults = {2, 3};
    if (run_lift_test<4, 71, mults>(std::vector<int>{20, 12, 16})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 4: K=4, P=101, mults=[2,3,5]" << std::endl;
    constexpr std::array mults = {2, 3, 5};
    if (run_lift_test<4, 101, mults>(std::vector<int>{12, 8, 16, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 5: K=5, P=127, mults=[2]" << std::endl;
    constexpr std::array mults = {2};
    if (run_lift_test<5, 127, mults>(std::vector<int>{15, 10})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 6: K=5, P=157, mults=[2,3]" << std::endl;
    constexpr std::array mults = {2, 3};
    if (run_lift_test<5, 157, mults>(std::vector<int>{25, 10, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 7: K=6, P=181, mults=[2]" << std::endl;
    constexpr std::array mults = {2};
    if (run_lift_test<6, 181, mults>(std::vector<int>{18, 6})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 8: K=8, P=211, mults=[2,3,5]" << std::endl;
    constexpr std::array mults = {2, 3, 5};
    if (run_lift_test<8, 211, mults>(std::vector<int>{176, 8, 16, 64})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 9: K=8, P=47, mults=[3,3]" << std::endl;
    constexpr std::array mults = {3, 3};
    if (run_lift_test<8, 47, mults>(std::vector<int>{9532, 7852, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 10: K=8, P=53, mults=[2,2,3,3]" << std::endl;
    constexpr std::array mults = {2, 2, 3, 3};
    if (run_lift_test<8, 53, mults>(std::vector<int>{1504, 1320, 112, 32, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 11: K=8, P=59, mults=[3,3]" << std::endl;
    constexpr std::array mults = {3, 3};
    if (run_lift_test<8, 59, mults>(std::vector<int>{15420, 1240, 0})) pass_count++;
    test_count++;
  }
  
  {
    std::cout << "Test 12: K=9, P=53, mults=[2,2,3,5]" << std::endl;
    constexpr std::array mults = {2, 2, 3, 5};
    if (run_lift_test<9, 53, mults>(std::vector<int>{38851, 125874, 5738, 36, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 13: K=9, P=59, mults=[2,2,3,5]" << std::endl;
    constexpr std::array mults = {2, 2, 3, 5};
    if (run_lift_test<9, 59, mults>(std::vector<int>{7255, 6093, 212, 36, 0})) pass_count++;
    test_count++;
  }

  {
    std::cout << "Test 14: K=9, P=67, mults=[2,2,3,5]" << std::endl;
    constexpr std::array mults = {2, 2, 3, 5};
    if (run_lift_test<9, 67, mults>(std::vector<int>{55282, 58082, 266, 36, 0})) pass_count++;
    test_count++;
  }

  std::cout << "\n" << std::string(60, '=') << std::endl;
  std::cout << "Results: " << pass_count << "/" << test_count << " tests passed" << std::endl;
  std::cout << std::string(60, '=') << std::endl;

  return (pass_count == test_count) ? 0 : 1;
}
