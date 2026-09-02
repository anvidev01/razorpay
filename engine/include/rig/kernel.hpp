// The deterministic policy kernel.
//
// INVARIANT: a pure function of its inputs. No allocation, no syscall, no lock,
// no clock read, no floating point. `now_ns` is sampled once by the caller and
// passed in. Identical inputs therefore produce a byte-identical verdict on any
// machine, forever -- which is what makes offline replay auditing possible.
// Determinism here is the AUDIT feature, not a performance feature.
#pragma once
#include "schema.hpp"

namespace rig {

// Branch-free constraint lookup over a contiguous, single-cache-line domain.
inline int find_constraint(const IntentSchema& s, std::uint32_t sku) noexcept {
  int found = -1;
  for (int i = 0; i < MAXC; ++i) {
    const bool hit = (i < static_cast<int>(s.n_constraints)) & (s.sku_id[i] == sku);
    found = hit ? i : found;                       // cmov, not a jump
  }
  return found;
}

Verdict evaluate(const IntentSchema& s, const CartView& c, std::uint64_t now_ns) noexcept;

}  // namespace rig
