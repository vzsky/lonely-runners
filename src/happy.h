#pragma once

#include <algorithm>
#include <array>
#include <future>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

#include "utils.h"

template <int K, int P> class LonelyRunnerChecker
{
  static_assert(K != 1, "K=1 is trivial and not supported");

public:
  LonelyRunnerChecker()
  {
    build_r_vectors();
    setup_tasks();
  }

  bool run()
  {
    bool any_unhappy = false;
    timeit([&]
    {
      unsigned int num_cores = std::thread::hardware_concurrency();

      std::vector<std::vector<std::array<uint16_t, 2>>> chunks(num_cores);
      for (size_t i = 0; i < tasks.size(); ++i) chunks[i % num_cores].push_back(tasks[i]);

      std::vector<std::future<bool>> futures;
      futures.reserve(num_cores);
      for (auto& chunk : chunks)
      {
        futures.push_back(std::async(std::launch::async, [this, chunk]()
        {
          return std::any_of(chunk.begin(), chunk.end(),
                             [this](const auto& prefix) { return process_task(prefix); });
        }));
      }

      any_unhappy = std::any_of(futures.begin(), futures.end(), [](std::future<bool>& f) { return f.get(); });
    });

    if (!any_unhappy)
      std::cout << "✅ K=" << K << ", P=" << P << " - All tuples happy!" << std::endl;
    else
      std::cout << "❌ K=" << K << ", P=" << P << " - Found unhappy tuple!" << std::endl;

    return !any_unhappy;
  }

private:
  std::vector<std::array<uint16_t, K>> R_vecs;
  std::vector<std::array<uint16_t, 2>> tasks;

  void build_r_vectors()
  {
    std::set<std::array<uint16_t, K>> unique_R;
    for (long long r = 1; r < P; ++r)
    {
      std::array<uint16_t, K> current_R{};
      for (long long i = 1; i <= K; ++i)
      {
        long long rem    = (r * i) % P;
        current_R[i - 1] = ((K + 1) * rem) / P;
      }
      unique_R.insert(current_R);
    }
    R_vecs.assign(unique_R.begin(), unique_R.end());
  }

  void setup_tasks()
  {
    for (int v0 = 0; v0 <= K; ++v0)
      for (int v1 = 0; v1 <= K; ++v1)
      {
        int z_count = (v0 == 0 ? 1 : 0) + (v1 == 0 ? 1 : 0);
        if (z_count * 2 < K + 1) tasks.push_back({(uint16_t)v0, (uint16_t)v1});
      }
  }

  bool is_happy(const std::array<uint16_t, K>& b) const
  {
    // R(0) special case
    for (int s = 0; s < K; ++s)
    {
      if (!std::ranges::any_of(b, [&](auto elem) { return (s * elem) % (K + 1) == 0; })) return true;
    }

    // R(r) standard cases
    for (const auto& R : R_vecs)
    {
      for (int s = 0; s < K; ++s)
      {
        bool ok = true;
        for (int i = 0; i < K; ++i)
        {
          int val = (s * b[i] + R[i]) % (K + 1);
          if (val == 0 || val == K)
          {
            ok = false;
            break;
          }
        }
        if (ok) return true;
      }
    }
    return false;
  }

  bool generate_and_check(int index, std::array<uint16_t, K>& b, int zero_count) const
  {
    if (index == K)
    {
      if (zero_count == 0) return false;
      return !is_happy(b);
    }

    for (int val = 0; val <= K; ++val)
    {
      b[index]      = val;
      int new_zeros = zero_count + (val == 0 ? 1 : 0);
      if (new_zeros * 2 >= K + 1) continue;
      if (generate_and_check(index + 1, b, new_zeros)) return true;
    }
    return false;
  }

  bool process_task(const std::array<uint16_t, 2>& prefix) const
  {
    std::array<uint16_t, K> b{};
    b[0]              = prefix[0];
    b[1]              = prefix[1];
    int initial_zeros = 0;
    if (prefix[0] == 0) initial_zeros++;
    if (prefix[1] == 0) initial_zeros++;
    return generate_and_check(2, b, initial_zeros);
  }
};
