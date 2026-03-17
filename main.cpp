#include <algorithm>
#include <bitset>
#include <cassert>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "src/speedset.h"
#include "src/utils.h"
#include "src/find_cover.h"
#include "src/lift.h"

struct Config
{
  enum class Type
  {
    Force,
    Maybe,
    Project,
  } type;
  int prime;
};

// Main driver: constructs and applies the lifting sieve over levels
template <int K, int P, std::array config> bool check_prime(int thread_id = 0)
{
  std::cout << std::format("[THREAD {}] now={}\n", thread_id, print_time());
  std::cout << std::format("[THREAD {}] Parameters: p = {}, k = {}", thread_id, P, K) << std::endl;

  SetOfSpeedSets<K> S;

  // Step 1: Compute seed covers S at level n = 1 (half-range mod p)
  timeit([&]
  {
    find_cover::CoverageArray<P> cov = find_cover::make_stationary_runner_coverage_mask<K, P>();
    S                                = find_cover::find_all_covers_parallel<K, P>(cov);
    std::cout << std::format("[THREAD {}] Step1 (n=1): S size = {}", thread_id, S.size()) << std::endl;
  });

  // Step 2 to N: Lift each seed from S using multiplier p, m =
  int last_skip = 1;
  int current_n = 1;
  for (auto [type, mult] : config)
  {
    if (mult == last_skip) continue;
    timeit([&]
    {
      lift::Context C2 = lift::make_context(P, K, current_n * mult, true);
      auto T           = lift::find_lifted_covers_parallel(C2, S, mult);
      std::cout << std::format("[THREAD {}] trying {}: T size = {}", thread_id, mult, T.size()) << std::endl;
      // std::cout << T << std::endl;
      // todo: project need not intersect. just project it down.
      if (type == Config::Type::Project)
      {
        SetOfSpeedSets<K> A, B, I;
        for (auto elem : S)
        {
          A.insert(elem.project(P).get_sorted_set());
        }
        for (auto elem : T)
        {
          B.insert(elem.project(P).get_sorted_set());
        }
        for (auto elem : A)
        {
          if (B.count(elem)) I.insert(elem);
        }
        S = std::move(I);
        // std::cout << S << std::endl;
        current_n = 1;
      }
      else if (type == Config::Type::Force || T.size() <= S.size())
      {
        S = std::move(T);
        current_n *= mult;
      }
      else
      {
        last_skip = mult;
      }
      std::cout << std::format("[THREAD {}] => (n={}): S size = {}", thread_id, current_n, S.size())
                << std::endl;
    });
  }

  std::cout << S << std::endl;

  // Final result: if S is empty then LRC verified for this p
  if (!S.empty())
  {
    std::cout << std::format("[THREAD {}] Counter Example Mod {}", thread_id, P) << std::endl;
    return 1;
  }
  std::cout << std::endl;
  return 0;
}

template <int K, std::array primes, std::array configs, std::size_t... Is>
void roll_works(std::index_sequence<Is...>)
{
  (check_prime<K, primes[Is], configs>(), ...);
}

int main()
{
  [[maybe_unused]] constexpr auto Force   = [](int p) { return Config{Config::Type::Force, p}; };
  [[maybe_unused]] constexpr auto Maybe   = [](int p) { return Config{Config::Type::Maybe, p}; };
  [[maybe_unused]] constexpr auto Project = [](int p) { return Config{Config::Type::Project, p}; };

  // constexpr int K             = 8;
  // constexpr std::array primes = {47,  53,  59,  61,  67,  71,  73,  79,  83,  89,  97,  101, 103,
  //                                107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
  //                                179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241};
  // // unexpected but this is faster than mult={3,3},
  // constexpr std::array config = {Maybe(2), Maybe(2), Force(3), Force(3)};
  // // constexpr std::array config = {Force(3), Force(3)};

  // constexpr int K             = 9;
  // constexpr std::array primes = {19,  53,  59,  67,  71,  73,  79,  83,  89,  97,  101, 103, 107,
  //                                109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179,
  //                                181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251,
                        
  //                                257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317};
  // constexpr std::array config = {Force(2), Maybe(2), Maybe(3), Force(5)};

  constexpr int K             = 10;
  constexpr std::array primes = { 
    293, 317, 331, 337, 349, 353, 359, 367, 373, 383, 389, 397, 401, 409, 419, 421, 431};
    // 103, 131, 137, 139, 149, 151, 157, 107, 109, 127, 163, 167, 173,
    //                              179, 433, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 251,
    //                              257, 263, 269, 271, 277, 281, 283, 307, 311, 313, 347, 379, 433,
    //                              439, 443, 449, 457, 461, 241, 293, 293, 317, 331, 337, 349, 353,
    //                              359, 367, 373, 383, 389, 397, 401, 409, 419, 421, 431};
  constexpr std::array config = {Force(2), Force(2), Project(2)};

  // constexpr int K             = 11;
  // constexpr std::array primes = {
  //     577, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 331, 337, 347, 349, 353, 359, 367, 379,
  //     383, 557, 563, 569, 571, 389, 149, 157, 181, 269, 23,  131, 137, 139, 151, 163, 167, 173, 179,
  //     191, 193, 197, 199, 271, 277, 281, 283, 293, 307, 311, 313, 317, 373, 397, 401, 409, 419, 421,
  //     431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547,
  // };
  // constexpr std::array config = {Force(2), Force(2), Maybe(2), Maybe(2), Force(3), Maybe(3)};

  roll_works<K, primes, config>(std::make_index_sequence<primes.size()>{});
}
