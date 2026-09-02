#include "rig/pct.hpp"
#include <cstring>

namespace rig {

const char* pct_status_name(PctStatus s) noexcept {
  switch (s) {
    case PctStatus::VALID:           return "VALID";
    case PctStatus::MISSING:         return "PCT missing";
    case PctStatus::BAD_SIGNATURE:   return "PCT signature invalid";
    case PctStatus::EXPIRED:         return "PCT expired";
    case PctStatus::NONCE_BURNED:    return "nonce already burned";
    case PctStatus::CART_MISMATCH:   return "cart_hash mismatch";
    case PctStatus::AMOUNT_MISMATCH: return "amount mismatch";
    default:                         return "unknown";
  }
}

Pct pct_mint(const Signer& signer, const PctBody& body) {
  Pct p;
  p.body = body;
  p.sig  = signer.sign(&p.body, sizeof(PctBody));
  return p;
}

PctStatus Executor::authorize(const Pct* pct, const Hash256& actual_cart_hash,
                              std::int64_t actual_amount, std::uint64_t now_ns) {
  const auto refuse = [&](PctStatus s) { ++refused_; return s; };

  if (!pct) return refuse(PctStatus::MISSING);
  if (!key_.verify(&pct->body, sizeof(PctBody), pct->sig))
    return refuse(PctStatus::BAD_SIGNATURE);
  if (now_ns > pct->body.exp_ns)
    return refuse(PctStatus::EXPIRED);
  if (burned_.count(pct->body.nonce))
    return refuse(PctStatus::NONCE_BURNED);
  // Re-hash what we are ABOUT to submit. This is what stops a cart swap between
  // approval and execution: an approved lunch cannot be exchanged for a blender.
  if (std::memcmp(pct->body.cart_hash, actual_cart_hash.data(), 32) != 0)
    return refuse(PctStatus::CART_MISMATCH);
  if (pct->body.amount_paise != actual_amount)
    return refuse(PctStatus::AMOUNT_MISMATCH);

  burned_.insert(pct->body.nonce);
  ++authorized_;
  return PctStatus::VALID;
}

}  // namespace rig
