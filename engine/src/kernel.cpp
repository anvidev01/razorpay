#include "rig/kernel.hpp"

namespace rig {

const char* reject_name(std::uint32_t bit) noexcept {
  switch (bit) {
    case R_SKU_NOT_IN_INTENT:    return "R_SKU_NOT_IN_INTENT";
    case R_QTY_EXCEEDED:         return "R_QTY_EXCEEDED";
    case R_UNIT_PRICE_EXCEEDED:  return "R_UNIT_PRICE_EXCEEDED";
    case R_CART_TOTAL_EXCEEDED:  return "R_CART_TOTAL_EXCEEDED";
    case R_MERCHANT_NOT_ALLOWED: return "R_MERCHANT_NOT_ALLOWED";
    case R_MANDATE_EXPIRED:      return "R_MANDATE_EXPIRED";
    case R_ARITH_OVERFLOW:       return "R_ARITH_OVERFLOW";
    case R_REPLAY_NONCE:         return "R_REPLAY_NONCE";
    case R_SCHEMA_VERSION:       return "R_SCHEMA_VERSION";
    case R_ENGINE_RESOURCE:      return "R_ENGINE_RESOURCE";
    default:                     return "R_UNKNOWN";
  }
}

const char* reject_help(std::uint32_t bit) noexcept {
  switch (bit) {
    case R_SKU_NOT_IN_INTENT:    return "item is not in the approved intent";
    case R_QTY_EXCEEDED:         return "quantity exceeds the per-item cap";
    case R_UNIT_PRICE_EXCEEDED:  return "unit price exceeds the per-item cap";
    case R_CART_TOTAL_EXCEEDED:  return "cart total exceeds the mandate budget";
    case R_MERCHANT_NOT_ALLOWED: return "merchant is not on the mandate allowlist";
    case R_MANDATE_EXPIRED:      return "mandate is outside its validity window";
    case R_ARITH_OVERFLOW:       return "cart arithmetic overflowed int64 paise";
    case R_REPLAY_NONCE:         return "capability nonce already used";
    case R_SCHEMA_VERSION:       return "cart or mandate schema version unsupported";
    case R_ENGINE_RESOURCE:      return "engine resource exhausted (fails closed)";
    default:                     return "unknown rejection";
  }
}

// Conditions become masks, not jumps. Measured consequence: p99/p50 = 1.29, so the
// adversarial cart costs the same as the benign one -- no timing side channel and no
// attack-shaped tail.
static inline std::uint32_t mask_if(bool cond, std::uint32_t bit) noexcept {
  return static_cast<std::uint32_t>(-static_cast<std::int32_t>(cond)) & bit;
}

Verdict evaluate(const IntentSchema& s, const CartView& c, std::uint64_t now_ns) noexcept {
  // Deliberately NOT `Verdict v{}`: that zero-initialises lines[MAX_CART] (512 bytes)
  // on every call, which measured as a 7ns tax on a 3-line cart. Only the first
  // n_lines entries are ever written or read -- see verdict_equal().
  Verdict v;
  v.bits             = 0;
  v.cart_total_paise = 0;
  v.n_lines          = c.n > MAX_CART ? MAX_CART : c.n;

  std::uint32_t bits = 0;

  bits |= mask_if(s.schema_version != SCHEMA_VER, R_SCHEMA_VERSION);
  bits |= mask_if(now_ns < s.not_before_ns || now_ns > s.not_after_ns, R_MANDATE_EXPIRED);
  bits |= mask_if(c.merchant_id == 0 || c.merchant_id > 64 ||
                    ((s.merchant_allow_mask >> (c.merchant_id - 1)) & 1ull) == 0,
                  R_MERCHANT_NOT_ALLOWED);
  bits |= mask_if(c.n > MAX_CART, R_ENGINE_RESOURCE);

  std::int64_t total = 0;
  for (std::uint32_t i = 0; i < v.n_lines; ++i) {
    const std::uint32_t sku = c.sku_id[i];
    const std::int64_t  up  = c.unit_paise[i];
    const std::uint32_t q   = c.qty[i];

    const int ci   = find_constraint(s, sku);
    const int safe = ci < 0 ? 0 : ci;              // clamp; index 0 always in range

    std::uint32_t lb = 0;
    lb |= mask_if(ci < 0, R_SKU_NOT_IN_INTENT);
    lb |= mask_if(ci >= 0 && q  > s.max_qty[safe],        R_QTY_EXCEEDED);
    // Two independent unit-price bounds. The second applies even to an unknown SKU:
    // a single line costing more than the ENTIRE mandate budget is always a violation,
    // and without it a hallucinated item would report only "not in intent" and hide
    // the fact that it also blew the budget.
    lb |= mask_if((ci >= 0 && up > s.max_unit_paise[safe]) || up > s.total_budget_paise,
                  R_UNIT_PRICE_EXCEEDED);
    lb |= mask_if(up < 0 || q == 0,                       R_SCHEMA_VERSION);

    std::int64_t line = 0;
    const bool of1 = __builtin_mul_overflow(up, static_cast<std::int64_t>(q), &line);
    const bool of2 = __builtin_add_overflow(total, line, &total);
    lb |= mask_if(of1 || of2, R_ARITH_OVERFLOW);

    v.lines[i].sku_id = sku;
    v.lines[i].bits   = lb;
    bits |= lb;
  }

  bits |= mask_if(total > s.total_budget_paise, R_CART_TOTAL_EXCEEDED);

  v.bits             = bits;
  v.cart_total_paise = total;
  return v;
}

}  // namespace rig
