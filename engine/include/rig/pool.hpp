// Fixed-capacity object pool with an index-based intrusive free list.
// Indices, not pointers: 4 bytes instead of 8, relocatable, and range-checkable.
#pragma once
#include <cstdint>
#include <cstddef>
#include <new>
#include <type_traits>

namespace rig {

template <class T, std::uint32_t N>
class FixedPool {
  static_assert(std::is_trivially_destructible_v<T>, "pool slots must be trivially destructible");
public:
  static constexpr std::uint32_t SENTINEL = 0xFFFFFFFFu;

  FixedPool() noexcept {
    for (std::uint32_t i = 0; i < N; ++i) next_[i] = i + 1;
    next_[N - 1] = SENTINEL;
    free_head_   = 0;
  }

  std::uint32_t acquire() noexcept {
    if (free_head_ == SENTINEL) return SENTINEL;   // exhausted -> caller denies
    const std::uint32_t idx = free_head_;
    free_head_ = next_[idx];
    ++live_;
    return idx;
  }

  void release(std::uint32_t idx) noexcept {
    if (idx >= N) return;
    next_[idx] = free_head_;
    free_head_ = idx;
    --live_;
  }

  T&       at(std::uint32_t i)       noexcept { return slots()[i]; }
  const T& at(std::uint32_t i) const noexcept { return slots()[i]; }
  bool     valid(std::uint32_t i) const noexcept { return i < N; }
  std::uint32_t live() const noexcept { return live_; }

private:
  T*       slots()       noexcept { return reinterpret_cast<T*>(storage_); }
  const T* slots() const noexcept { return reinterpret_cast<const T*>(storage_); }

  alignas(128) std::byte storage_[std::size_t(N) * sizeof(T)];
  std::uint32_t next_[N];
  std::uint32_t free_head_ = SENTINEL;
  std::uint32_t live_      = 0;
};

}  // namespace rig
