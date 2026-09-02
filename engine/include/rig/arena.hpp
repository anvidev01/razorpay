// Per-request bump allocator. Allocation is a pointer bump; deallocation is one store.
// Poisons itself under ASan so a use-after-reset inside the pool is VISIBLE.
// See docs/04-INCIDENT-2AM.md -- this instrumentation is why that bug cannot recur.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/asan_interface.h>
#    define RIG_ASAN 1
#  endif
#endif
#ifndef ASAN_POISON_MEMORY_REGION
#  define ASAN_POISON_MEMORY_REGION(a, b)   ((void)(a), (void)(b))
#  define ASAN_UNPOISON_MEMORY_REGION(a, b) ((void)(a), (void)(b))
#endif

namespace rig {

class ScratchArena {
public:
  explicit ScratchArena(std::size_t cap)
      : base_(static_cast<std::byte*>(std::aligned_alloc(128, (cap + 127) & ~std::size_t(127)))),
        cap_((cap + 127) & ~std::size_t(127)) {
    if (!base_) std::abort();
    ASAN_POISON_MEMORY_REGION(base_, cap_);
  }
  ~ScratchArena() {
    ASAN_UNPOISON_MEMORY_REGION(base_, cap_);
    std::free(base_);
  }
  ScratchArena(const ScratchArena&)            = delete;
  ScratchArena& operator=(const ScratchArena&) = delete;

  // Returns nullptr on exhaustion. Callers MUST treat that as R_ENGINE_RESOURCE (deny).
  template <class T>
  T* alloc(std::size_t n) noexcept {
    const std::size_t off  = (head_ + 63) & ~std::size_t(63);   // 64B align every array
    const std::size_t need = n * sizeof(T);
    if (off + need > cap_) return nullptr;                      // fail closed, never grow
    head_ = off + need;
    std::byte* p = base_ + off;
    ASAN_UNPOISON_MEMORY_REGION(p, need);
    return reinterpret_cast<T*>(p);
  }

  void reset() noexcept {
    ASAN_POISON_MEMORY_REGION(base_, head_);   // the key line
    head_ = 0;
    ++gen_;                                    // invalidates every outstanding handle
  }

  std::uint32_t generation() const noexcept { return gen_; }
  std::size_t   used()       const noexcept { return head_; }
  std::size_t   capacity()   const noexcept { return cap_; }

private:
  std::byte*    base_;
  std::size_t   cap_;
  std::size_t   head_ = 0;
  std::uint32_t gen_  = 1;
};

}  // namespace rig
