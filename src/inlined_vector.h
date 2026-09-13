#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

template <typename T, std::size_t Capacity>
  requires std::is_trivial_v<T>
class InlinedVector
{
  std::array<T, Capacity> _data;
  std::size_t _size = 0;

public:
  void push_back(const T& v) { _data[_size++] = v; }
  void push_back(T&& v) { _data[_size++] = std::move(v); }

  template <typename... Args> T& emplace_back(Args&&... args)
  {
    _data[_size] = T(std::forward<Args>(args)...);
    return _data[_size++];
  }

  std::size_t size() const { return _size; }
  bool empty() const { return _size == 0; }
  void clear() { _size = 0; }

  T& operator[](std::size_t i) { return _data[i]; }
  const T& operator[](std::size_t i) const { return _data[i]; }

  T* begin() { return _data.begin(); }
  T* end() { return _data.begin() + _size; }
  const T* begin() const { return _data.begin(); }
  const T* end() const { return _data.begin() + _size; }
};
