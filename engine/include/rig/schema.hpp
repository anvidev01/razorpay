// The signed mandate, the proposed cart, and the verdict vocabulary.
#pragma once
#include <cstdint>
#include <cstddef>

namespace rig {

// Verdict bits. The kernel ACCUMULATES these -- it never short-circuits -- so a cart
// that violates three rules reports all three. That is what makes the audit trail
// explainable rather than first-failure-only.
// NOTE: R_OK is deliberately NOT used as a name here -- <unistd.h> defines R_OK as a
// macro (test for read permission), so any translation unit including both would fail
// to compile. R_NONE is the zero verdict.
enum Reject : std::uint32_t {
  R_NONE                   = 0u,
  R_SKU_NOT_IN_INTENT    = 1u << 0,
  R_QTY_EXCEEDED         = 1u << 1,
  R_UNIT_PRICE_EXCEEDED  = 1u << 2,
  R_CART_TOTAL_EXCEEDED  = 1u << 3,
  R_MERCHANT_NOT_ALLOWED = 1u << 4,
  R_MANDATE_EXPIRED      = 1u << 5,
  R_ARITH_OVERFLOW       = 1u << 6,
  R_REPLAY_NONCE         = 1u << 7,
  R_SCHEMA_VERSION       = 1u << 8,
  R_ENGINE_RESOURCE      = 1u << 9,
  // --- agentic-specific failure modes (see docs/06-AGENTIC-BLIND-SPOTS.md) ---
  R_SUBSTITUTION_DENIED  = 1u << 10,  // swap not permitted by the mandate
  R_SUBSTITUTION_DELTA   = 1u << 11,  // swap allowed in kind, but too expensive
  R_INJECTION_SUSPECTED  = 1u << 12,  // instruction-like text in a cart field
  R_DUPLICATE_CHARGE     = 1u << 13,  // same semantic cart already authorised
  // --- Track 02 behavioural signals. These ESCALATE, they never auto-block. ---
  R_VELOCITY_ANOMALY     = 1u << 14,  // spending far above this agent's baseline
  R_NEW_MERCHANT         = 1u << 15,  // merchant never seen for this agent
  R_ODD_HOUR             = 1u << 16,  // outside the agent's normal active hours
  R_BIT_COUNT            = 17
};

// Three outcomes, not two. A hard intent violation is a DENY. A behavioural signal
// is a REVIEW -- it asks the human rather than killing a legitimate purchase.
// Auto-blocking on a probabilistic signal is how a fraud system destroys good revenue,
// so the false-positive cost here is one confirmation tap, not a lost sale.
enum class Outcome : std::uint8_t { ALLOW = 0, REVIEW = 1, DENY = 2 };
const char* outcome_name(Outcome o) noexcept;

// Deterministic, bounded rule violations -> DENY.
inline constexpr std::uint32_t HARD_DENY_MASK =
    R_SKU_NOT_IN_INTENT | R_QTY_EXCEEDED | R_UNIT_PRICE_EXCEEDED | R_CART_TOTAL_EXCEEDED |
    R_MERCHANT_NOT_ALLOWED | R_MANDATE_EXPIRED | R_ARITH_OVERFLOW | R_REPLAY_NONCE |
    R_SCHEMA_VERSION | R_ENGINE_RESOURCE | R_SUBSTITUTION_DENIED | R_SUBSTITUTION_DELTA;

// Probabilistic / heuristic signals -> step up to the human.
// R_INJECTION_SUSPECTED is here deliberately: the scanner is a heuristic, so using it
// to auto-block would manufacture false positives. The STRUCTURAL defence (the item is
// not in the signed mandate) is already in HARD_DENY_MASK and does the real work.
inline constexpr std::uint32_t REVIEW_MASK =
    R_INJECTION_SUSPECTED | R_VELOCITY_ANOMALY | R_NEW_MERCHANT | R_ODD_HOUR;

// Bits derived from CROSS-TRANSACTION STATE rather than from this record's own inputs.
// The kernel is a pure function of one record, so it cannot re-derive these, and the
// replay auditor must exclude them or it would report false divergences.
// R_INJECTION_SUSPECTED is deliberately NOT here: it is computed from this cart's own
// text at parse time and travels in the payload, so it replays exactly.
inline constexpr std::uint32_t STATEFUL_RISK_MASK =
    R_VELOCITY_ANOMALY | R_NEW_MERCHANT | R_ODD_HOUR;

inline Outcome classify(std::uint32_t bits) noexcept {
  if (bits & HARD_DENY_MASK) return Outcome::DENY;
  if (bits & REVIEW_MASK)    return Outcome::REVIEW;
  return Outcome::ALLOW;
}

// What the mandate permits when the agent cannot get the exact SKU.
// Grocery inventory turns hourly, so "deny every substitution" is unusable in
// practice and "allow anything" is how a 60-rupee milk becomes a 180-rupee one.
enum SubstPolicy : std::uint8_t {
  SUBST_DENY          = 0,   // exact SKUs only
  SUBST_SAME_CATEGORY = 1,   // same category, within max_delta_bp of the capped price
  SUBST_ANY_IN_BUDGET = 2,   // any item, still bounded by every price cap
};

const char* reject_name(std::uint32_t bit) noexcept;
const char* reject_help(std::uint32_t bit) noexcept;

inline constexpr int   MAXC          = 16;   // constraints per mandate
inline constexpr int   MAX_CART      = 64;   // lines per cart
inline constexpr int   SCHEMA_VER    = 1;

// One mandate. POD, cache-aligned, no pointers -- so it can be memcpy'd into the WAL
// and replayed byte-for-byte by the auditor.
struct alignas(128) IntentSchema {
  std::uint64_t mandate_id;
  std::uint64_t not_before_ns;
  std::uint64_t not_after_ns;
  std::int64_t  total_budget_paise;      // integer paise. never a float.
  std::uint64_t merchant_allow_mask;     // bitset over interned merchant ids 1..64
  std::uint32_t n_constraints;
  std::uint32_t schema_version;
  std::uint32_t sku_id[MAXC];            // interned, contiguous: one cache line
  std::int64_t  max_unit_paise[MAXC];
  std::uint32_t max_qty[MAXC];
  std::uint32_t category_id[MAXC];       // interned category, for substitution matching
  std::uint16_t subst_max_delta_bp;      // basis points a substitute may exceed the cap
  std::uint8_t  subst_policy;            // SubstPolicy
  std::uint8_t  _pad1;
  std::uint8_t  integrity_tag[16];       // SipHash-2-4 over the record, checked per cart
};

// SoA cart. Arrays are arena-backed and 64B aligned.
struct CartView {
  const std::uint32_t* sku_id;
  const std::int64_t*  unit_paise;
  const std::uint32_t* qty;
  const std::uint32_t* category_id;      // interned; 0 when the merchant sent none
  std::uint32_t        n;
  std::uint32_t        merchant_id;      // interned
  std::uint32_t        text_flags;       // injection scan result from the parse boundary
  std::uint32_t        _pad;
};

// Per-line attribution, so the audit record can name WHICH line failed and why.
struct LineVerdict {
  std::uint32_t sku_id;
  std::uint32_t bits;
  std::uint32_t substituted_for;   // the intended SKU this line stands in for, else 0
  std::uint32_t _pad;
};

struct Verdict {
  std::uint32_t bits;                    // OR of all Reject bits
  std::int64_t  cart_total_paise;
  std::uint32_t n_lines;
  LineVerdict   lines[MAX_CART];
  bool allowed() const noexcept { return bits == R_NONE; }
};

// Compares only the meaningful portion. lines[n_lines..MAX_CART) is intentionally
// left unwritten by evaluate() for speed, so a raw memcmp over Verdict is invalid.
inline bool verdict_equal(const Verdict& a, const Verdict& b) noexcept {
  if (a.bits != b.bits || a.cart_total_paise != b.cart_total_paise || a.n_lines != b.n_lines)
    return false;
  for (std::uint32_t i = 0; i < a.n_lines; ++i)
    if (a.lines[i].sku_id != b.lines[i].sku_id || a.lines[i].bits != b.lines[i].bits)
      return false;
  return true;
}

}  // namespace rig
