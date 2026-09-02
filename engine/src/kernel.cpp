#include "rig/kernel.hpp"

namespace rig {

const char* outcome_name(Outcome o) noexcept {
  switch (o) {
    case Outcome::ALLOW:  return "ALLOW";
    case Outcome::REVIEW: return "REVIEW";
    case Outcome::DENY:   return "DENY";
    default:              return "UNKNOWN";
  }
}

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
    case R_SUBSTITUTION_DENIED:  return "R_SUBSTITUTION_DENIED";
    case R_SUBSTITUTION_DELTA:   return "R_SUBSTITUTION_DELTA";
    case R_INJECTION_SUSPECTED:  return "R_INJECTION_SUSPECTED";
    case R_DUPLICATE_CHARGE:     return "R_DUPLICATE_CHARGE";
    case R_VELOCITY_ANOMALY:     return "R_VELOCITY_ANOMALY";
    case R_NEW_MERCHANT:         return "R_NEW_MERCHANT";
    case R_ODD_HOUR:             return "R_ODD_HOUR";
    case R_MANDATE_UNKNOWN:      return "R_MANDATE_UNKNOWN";
    case R_REVERSAL_UNAUTHORISED: return "R_REVERSAL_UNAUTHORISED";
    case R_REVERSAL_EXCEEDS:      return "R_REVERSAL_EXCEEDS";
    case R_REVERSAL_NO_PAYMENT:   return "R_REVERSAL_NO_PAYMENT";
    case R_REVERSAL_DUPLICATE:    return "R_REVERSAL_DUPLICATE";
    case R_SUBSCRIPTION_UNDISCLOSED: return "R_SUBSCRIPTION_UNDISCLOSED";
    case R_RECURRING_EXCEEDS:        return "R_RECURRING_EXCEEDS";
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
    case R_SUBSTITUTION_DENIED:  return "substitute is not in an approved category";
    case R_SUBSTITUTION_DELTA:   return "substitute costs more than the mandate allows";
    case R_INJECTION_SUSPECTED:  return "instruction-like text found in a cart field";
    case R_DUPLICATE_CHARGE:     return "this exact cart was already authorised";
    case R_VELOCITY_ANOMALY:     return "spending or frequency above this agent's baseline";
    case R_NEW_MERCHANT:         return "merchant never used by this agent before";
    case R_ODD_HOUR:             return "outside this agent's normal active hours";
    case R_MANDATE_UNKNOWN:      return "no mandate with this id has been signed and admitted";
    case R_REVERSAL_UNAUTHORISED: return "a refund must be signed by the human's device, not requested by the agent";
    case R_REVERSAL_EXCEEDS:      return "refund is larger than the purchase it reverses";
    case R_REVERSAL_NO_PAYMENT:   return "the original decision never resulted in a payment";
    case R_REVERSAL_DUPLICATE:    return "this purchase has already been reversed";
    case R_SUBSCRIPTION_UNDISCLOSED: return "this commits to a recurring charge the mandate never authorised";
    case R_RECURRING_EXCEEDS:        return "the recurring charge exceeds what was authorised per interval";
    default:                     return "unknown rejection";
  }
}

// Conditions become masks, not jumps. Measured consequence: p99/p50 = 1.29, so the
// adversarial cart costs the same as the benign one -- no timing side channel and no
// attack-shaped tail.
static inline std::uint32_t mask_if(bool cond, std::uint32_t bit) noexcept {
  return static_cast<std::uint32_t>(-static_cast<std::int32_t>(cond)) & bit;
}

// Finds a constraint in the same category -- the candidate this line substitutes for.
static inline int find_by_category(const IntentSchema& s, std::uint32_t cat) noexcept {
  int found = -1;
  if (cat == 0) return -1;                          // merchant sent no category: no swap
  for (int i = 0; i < MAXC; ++i) {
    const bool hit = (i < static_cast<int>(s.n_constraints)) & (s.category_id[i] == cat);
    found = hit ? i : found;
  }
  return found;
}

Verdict evaluate(const IntentSchema& s, const CartView& c, std::uint64_t now_ns) noexcept {
  // Deliberately NOT `Verdict v{}`: that zero-initialises lines[MAX_CART] (512 bytes)
  // on every call, which measured as a 7ns tax on a 3-line cart. Only the first
  // n_lines entries are ever written or read -- see verdict_equal().
  Verdict v;
  v.bits             = 0;
  v.cart_total_paise = 0;
  v.recurring_paise  = 0;
  v.n_lines          = c.n > MAX_CART ? MAX_CART : c.n;

  std::uint32_t bits = 0;

  bits |= mask_if(s.schema_version != SCHEMA_VER, R_SCHEMA_VERSION);
  bits |= mask_if(now_ns < s.not_before_ns || now_ns > s.not_after_ns, R_MANDATE_EXPIRED);
  bits |= mask_if(c.merchant_id == 0 || c.merchant_id > 64 ||
                    ((s.merchant_allow_mask >> (c.merchant_id - 1)) & 1ull) == 0,
                  R_MERCHANT_NOT_ALLOWED);
  bits |= mask_if(c.n > MAX_CART, R_ENGINE_RESOURCE);
  // An empty cart is not a purchase. It previously returned ALLOW and minted a
  // capability token bound to nothing, burning a nonce for a zero-value order.
  bits |= mask_if(c.n == 0, R_SCHEMA_VERSION);
  // Injection flags are raised at the parse boundary, where the text still exists.
  // NOTE: this is telemetry and defence in depth, NOT the security boundary --
  // see docs/06-AGENTIC-BLIND-SPOTS.md. The real control is that an injected item
  // is not in the signed mandate, so it fails on intent regardless of the text.
  bits |= (c.text_flags & R_INJECTION_SUSPECTED);

  // Quantity caps must be AGGREGATE across the cart, not per line. The agent decides
  // how many lines to send, so a per-line check lets it buy N of a max-1 item by
  // sending N lines of quantity 1. Found by testing; see docs/09-SECURITY-REVIEW.md.
  std::uint64_t agg_qty[MAXC] = {};
  int           line_eff[MAX_CART];

  std::int64_t total = 0;
  for (std::uint32_t i = 0; i < v.n_lines; ++i) {
    const std::uint32_t sku = c.sku_id[i];
    const std::int64_t  up  = c.unit_paise[i];
    const std::uint32_t q   = c.qty[i];

    const std::uint32_t cat = c.category_id ? c.category_id[i] : 0u;

    const int ci = find_constraint(s, sku);        // exact SKU
    // Substitution: only consulted when the exact SKU is absent.
    const int sub = (ci < 0 && s.subst_policy == SUBST_SAME_CATEGORY)
                        ? find_by_category(s, cat) : -1;
    const bool any_ok = (ci < 0 && s.subst_policy == SUBST_ANY_IN_BUDGET);

    // The constraint this line is judged against: exact, else its substitute.
    const int eff  = ci >= 0 ? ci : sub;
    const int safe = eff < 0 ? 0 : eff;            // clamp; index 0 always in range

    std::uint32_t lb = 0;
    // Unknown SKU is a violation unless a substitution rule adopts it.
    lb |= mask_if(ci < 0 && sub < 0 && !any_ok, R_SKU_NOT_IN_INTENT);
    // Policy permits swaps, but nothing in the mandate shares this category.
    lb |= mask_if(ci < 0 && sub < 0 && s.subst_policy == SUBST_SAME_CATEGORY,
                  R_SUBSTITUTION_DENIED);
    // A legitimate swap that costs too much. ceiling = cap * (1 + delta_bp/10000).
    // 60-rupee milk with a +20% allowance tops out at 72; a 180-rupee organic
    // substitute is in the right category and still refused.
    const std::int64_t cap     = s.max_unit_paise[safe];
    const std::int64_t ceiling = cap + (cap * static_cast<std::int64_t>(s.subst_max_delta_bp)) / 10000;
    lb |= mask_if(ci < 0 && sub >= 0 && up > ceiling, R_SUBSTITUTION_DELTA);

    // per-line cap (a single line over the limit is still a violation)
    lb |= mask_if(eff >= 0 && q  > s.max_qty[safe],        R_QTY_EXCEEDED);
    line_eff[i] = eff;
    if (eff >= 0) agg_qty[eff] += q;
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

    // A recurring commitment is not covered by the cart total, so it is checked on its
    // own terms. A 1-rupee trial that becomes 999 a month passes every price cap.
    const std::int64_t rec = c.recurring_paise ? c.recurring_paise[i] : 0;
    lb |= mask_if(rec > 0 && !s.allow_recurring,           R_SUBSCRIPTION_UNDISCLOSED);
    lb |= mask_if(rec > 0 && s.allow_recurring && rec > s.max_recurring_paise,
                                                            R_RECURRING_EXCEEDS);
    v.recurring_paise += rec > 0 ? rec : 0;

    v.lines[i].sku_id          = sku;
    v.lines[i].bits            = lb;
    v.lines[i].substituted_for = (ci < 0 && sub >= 0) ? s.sku_id[sub] : 0u;
    bits |= lb;
  }

  // Aggregate cap: sum every line that resolves to the same constraint.
  std::uint32_t over = 0;                      // bitmask of constraints exceeded
  for (int ci = 0; ci < MAXC; ++ci) {
    const bool bad = (ci < static_cast<int>(s.n_constraints)) &&
                     (agg_qty[ci] > s.max_qty[ci]);
    over |= static_cast<std::uint32_t>(bad) << ci;
    bits |= mask_if(bad, R_QTY_EXCEEDED);
  }
  // Attribute the overrun to every line that contributed to it.
  for (std::uint32_t i = 0; i < v.n_lines; ++i) {
    const int e = line_eff[i];
    if (e >= 0 && ((over >> e) & 1u)) v.lines[i].bits |= R_QTY_EXCEEDED;
  }

  bits |= mask_if(total > s.total_budget_paise, R_CART_TOTAL_EXCEEDED);

  v.bits             = bits;
  v.cart_total_paise = total;
  return v;
}

}  // namespace rig
