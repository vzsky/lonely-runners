#pragma once

#include <cstdint>

template <int N> struct Bitset
{
private:
  static constexpr int NW       = (N + 63) / 64;
  static constexpr int TAIL_REM = N % 64;

  static constexpr uint64_t TAIL_MASK = (TAIL_REM == 0) ? ~0ULL : ((1ULL << TAIL_REM) - 1);

  uint64_t w[NW] = {};

  struct BitRef
  {
    uint64_t& word;
    uint64_t mask;
    operator bool() const { return (word & mask) != 0; }
    BitRef& operator=(bool b)
    {
      if (b) word |= mask;
      else word &= ~mask;
      return *this;
    }
  };

public:
  BitRef operator[](int i) { return BitRef{w[i >> 6], 1ULL << (i & 63)}; }
  bool operator[](int i) const { return (w[i >> 6] >> (i & 63)) & 1; }

  void set(int i) { w[i >> 6] |= 1ULL << (i & 63); }
  void reset(int i) { w[i >> 6] &= ~(1ULL << (i & 63)); }
  bool test(int i) const { return (w[i >> 6] >> (i & 63)) & 1; }

  int count() const
  {
    int s = 0;
    for (int j = 0; j < NW; ++j) s += __builtin_popcountll(w[j]);
    return s;
  }

  bool any() const
  {
    for (int j = 0; j < NW; ++j) if (w[j]) return true;
    return false;
  }

  Bitset& operator|=(const Bitset& o)
  {
    for (int j = 0; j < NW; ++j) w[j] |= o.w[j];
    return *this;
  }

  Bitset& operator&=(const Bitset& o)
  {
    for (int j = 0; j < NW; ++j) w[j] &= o.w[j];
    return *this;
  }

  Bitset operator|(const Bitset& o) const
  {
    Bitset r = *this;
    r |= o;
    return r;
  }

  Bitset operator&(const Bitset& o) const
  {
    Bitset r = *this;
    r &= o;
    return r;
  }

  Bitset operator~() const
  {
    Bitset r = *this;
    for (int j = 0; j < NW; ++j) r.w[j] = ~r.w[j];
    r.w[NW - 1] &= TAIL_MASK;
    return r;
  }

  // for_each would run from low to high
  void for_each(auto&& f) const
  {
    for (int j = 0; j < NW; ++j)
    {
      uint64_t word = w[j];
      while (word)
      {
        int i = 64 * j + __builtin_ctzll(word);
        word &= word - 1;
        f(i);
      }
    }
  }
};
