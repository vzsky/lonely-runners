#pragma once

#include <array>
#include <unordered_set>

#include "utils.h"

// Forward-declared so fill_free_slots (below) can name SetOfSpeedSets<K>
// as its return type -- SpeedSet and SpeedSetHasher are each other's
// dependency (Hasher needs SpeedSet complete to iterate it, SpeedSet
// wants the alias before SpeedSetHasher exists), so the alias itself is
// declared early using still-incomplete types; that's fine, an alias
// doesn't need its arguments complete until actually used.
template <int K> struct SpeedSet;
template <int K> struct SpeedSetHasher;
template <int K> using SetOfSpeedSets = std::unordered_set<SpeedSet<K>, SpeedSetHasher<K>>;

template <int K> struct SpeedSet
{
private:
  std::array<int, K> mSet{};
  int mSize = 0;

public:
  SpeedSet() = default;

  SpeedSet(const std::array<int, K>& v) : mSize{K}
  {
    for (int i = 0; i < mSize; ++i) mSet[i] = v[i];
  }

  const auto begin() const { return mSet.begin(); }
  const auto end() const { return mSet.end(); }

  void insert(int x) { mSet[mSize++] = x; }
  void remove(int x)
  {
    --mSize;
    assert(x == mSet[mSize]);
  }

  int size() const { return mSize; }

  SpeedSet get_sorted_set() const
  {
    assert(mSize == K);
    SpeedSet tmp(*this);
    std::sort(tmp.mSet.begin(), tmp.mSet.begin() + tmp.mSize);
    return tmp;
  }

  SpeedSet get_canonical_representation(int prime) const
  {
    assert(mSize == K);

    std::array<long long, K> prefix;
    prefix[0] = mSet[0];
    for (int i = 1; i < K; ++i) prefix[i] = prefix[i - 1] * mSet[i] % prime;

    const auto is_less = [](const auto& a, const auto& b)
    {
      for (int i = 0; i < K; ++i)
        if (a[i] != b[i]) return a[i] < b[i];
      return false;
    };

    std::array<int, K> best;
    int run = mod_inverse(prefix[K - 1], prime);
    for (int i = K - 1; i >= 0; --i)
    {
      int inv = (i > 0) ? run * prefix[i - 1] % prime : run;
      if (i > 0) run = run * mSet[i] % prime;

      auto tmp = mSet;
      for (int j = 0; j < K; ++j)
      {
        int v  = static_cast<long long>(mSet[j]) * inv % prime;
        tmp[j] = std::min(v, prime - v);
      }
      std::sort(tmp.begin(), tmp.end());

      if (i == K - 1 || is_less(tmp, best)) best = tmp;
    }

    return best;
  }

  bool subset_gcd_implies_proper(long long n) const
  {
    // this doesn't require mSet ordering
    std::array<long long, K> pref, suf;

    pref[0] = mSet[0];
    for (int i = 1; i < K; ++i) pref[i] = gcd_fallback(pref[i - 1], (long long)mSet[i]);

    suf[K - 1] = mSet[K - 1];
    for (int i = K - 2; i >= 0; --i) suf[i] = gcd_fallback(suf[i + 1], (long long)mSet[i]);

    if (gcd_fallback(n, suf[1]) != 1) return true;
    if (gcd_fallback(n, pref[K - 2]) != 1) return true;

    for (int removed = 1; removed < K - 1; ++removed)
    {
      if (gcd_fallback(n, pref[removed - 1], suf[removed + 1]) != 1) return true;
    }

    return false;
  }

  bool operator==(const SpeedSet& o) const = default;

  SpeedSet<K> project(int p) const
  {
    SpeedSet<K> result(mSet);
    for (int i = 0; i < K; i++) result.mSet[i] %= p;
    return result;
  }

  // While there's a free slot, try every possible speed in it and
  // recurse -- coverage is already complete by the time this is called
  // (see Dfs::run), so any speed is a legal fill for the remaining slots.
  SetOfSpeedSets<K> fill_free_slots(int prime) const
  {
    SpeedSet<K> cur = *this;
    SetOfSpeedSets<K> results;
    cur.fill_free_slots_rec(prime, results);
    return results;
  }

private:
  void fill_free_slots_rec(int prime, SetOfSpeedSets<K>& results)
  {
    if (mSize == K)
    {
      results.insert(get_canonical_representation(prime));
      return;
    }

    for (int v = 1; v <= prime / 2; ++v)
    {
      insert(v);
      fill_free_slots_rec(prime, results);
      remove(v);
    }
  }
};

template <int K> std::ostream& operator<<(std::ostream& os, const SpeedSet<K>& s)
{
  for (auto x : s) os << x << ' ';
  return os;
}

template <int K> struct SpeedSetHasher
{
  size_t operator()(const SpeedSet<K>& v) const noexcept
  {
    size_t h = 0;
    for (int x : v)
    {
      h ^= std::hash<int>{}(x) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
  }
};

template <int K> std::ostream& operator<<(std::ostream& os, const SetOfSpeedSets<K>& s)
{
  for (auto x : s) os << "( " << x << " ), ";
  return os;
}
