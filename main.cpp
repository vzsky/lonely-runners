#include <algorithm>
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

struct Config
{
  enum class Type
  {
    Force,
    Maybe
  } type;
  int prime;
};

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

template <int K, int P> static SpeedSet<K> multiply_solution(const SpeedSet<K>& sol, int c)
{
  SpeedSet<K> result;
  for (int i = 0; i < K; ++i)
  {
    int v = (int)((1LL * sol.mSet[i] * c) % P);
    if (v > P / 2) v = P - v;
    result.insert(v);
  }
  return result.get_sorted_set();
}

template <int K, int P> static SetOfSpeedSets<K> expand_by_symmetry(const SetOfSpeedSets<K>& base_solutions)
{
  SetOfSpeedSets<K> all;
  all.reserve(base_solutions.size() * (P / 2));
  for (const auto& sol : base_solutions)
    for (int c = 1; c <= P / 2; ++c) all.insert(multiply_solution<K, P>(sol, c));
  return all;
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

  std::cout << "Base solutions (containing 1): " << base_solutions.size() << std::endl;
  return expand_by_symmetry<K, P>(base_solutions);
}

} // namespace find_cover

namespace lift
{

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
template <int K> SetOfSpeedSets<K> lift(const Context& C, const SpeedSet<K>& seed, int multiplier)
{

  SetOfSpeedSets<K> result;

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

  std::array<int, K> idx;
  idx.fill(-1);

  std::function<void(int)> dfs = [&](int depth)
  {
    if (depth == K)
    {
      // construct final_idx in natural order
      WordBitset acc(C.bitlen);
      for (int t = 0; t < K; ++t) acc.orWith(C.vec[idx[t]]);
      if (acc.count() != C.bitlen) return;

      SpeedSet<K> out = idx;
      if (out.subset_gcd_implies_proper(C.n)) return;

      result.insert(out.get_sorted_set());
      return;
    }
    int pos = order[depth];
    for (int candidate : cand[pos])
    {
      idx[pos] = candidate;
      dfs(depth + 1);
    }
    idx[pos] = -1;
  }; // end dfs

  dfs(0);

  return result;
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
      if (type == Config::Type::Force || T.size() <= S.size())
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
  constexpr auto Force = [](int p) { return Config{Config::Type::Force, p}; };
  constexpr auto Maybe = [](int p) { return Config{Config::Type::Maybe, p}; };

  // constexpr int K = 8;
  // std::vector<int> primes = {
  //     47,  53,  59,  61,  67,  71,  73,  79,  83,
  //     89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
  //     157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
  //     233, 239, 241};
  // // unexpected but this is faster than mult={3,3},
  // std::vector<Config> config = {Maybe(2), Maybe(2), Force(3), Force(3)};

  // constexpr int K =9;
  // std::vector<int> primes = {19,  53,  59,  67,  71,  73,  79,  83,  89,  97,  101, 103, 107,
  //                            109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179,
  //                            181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251,
  //                            257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317};
  // std::vector<int> primes = {179, 181, 191};
  // nothing stops us from using different config per prime
  // constexpr std::array config = {Force(2), Maybe(2), Maybe(3), Force(5)};

  constexpr int K             = 11;
  constexpr std::array primes = {
      577, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 331, 337, 347, 349, 353, 359, 367, 379,
      383, 557, 563, 569, 571, 389, 149, 157, 181, 269, 23,  131, 137, 139, 151, 163, 167, 173, 179,
      191, 193, 197, 199, 271, 277, 281, 283, 293, 307, 311, 313, 317, 373, 397, 401, 409, 419, 421,
      431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547,
  };
  constexpr std::array config = {Force(2), Force(2), Maybe(2), Maybe(2), Force(3), Maybe(3)};

  roll_works<K, primes, config>(std::make_index_sequence<primes.size()>{});
}
