// Payment Capability Token.
//
// The agent holds no Razorpay credentials. The ONLY way to move money is a PCT,
// and a PCT can only be minted by the policy engine AFTER the decision is durable
// in the WAL. Bypassing the policy therefore requires forging an Ed25519 signature,
// not winning an argument with a language model.
#pragma once
#include "crypto.hpp"
#include "schema.hpp"
#include <cstdint>
#include <unordered_set>
#include <string>

namespace rig {

#pragma pack(push, 1)
struct PctBody {
  std::uint64_t decision_id;
  std::uint64_t mandate_id;
  std::uint8_t  cart_hash[32];      // binds the token to THIS cart
  std::int64_t  amount_paise;
  std::uint32_t merchant_id;
  std::uint32_t _pad;
  std::uint64_t nonce;              // single use
  std::uint64_t exp_ns;
  std::uint64_t wal_seq;            // the durable decision record
  std::uint8_t  wal_record_hash[32];
};
#pragma pack(pop)

struct Pct {
  PctBody body;
  Sig512  sig;
};

enum class PctStatus {
  VALID = 0,
  MISSING,
  BAD_SIGNATURE,
  EXPIRED,
  NONCE_BURNED,
  CART_MISMATCH,
  AMOUNT_MISMATCH,
};
const char* pct_status_name(PctStatus s) noexcept;

Pct pct_mint(const Signer& signer, const PctBody& body);

// The Executor's gate. Stateful: burns the nonce on success.
class Executor {
public:
  explicit Executor(const Signer& gateway_key) : key_(gateway_key) {}

  // `actual_cart_hash` / `actual_amount` are what the executor is ABOUT to submit.
  // Re-checking them is what stops a cart swap between approval and execution.
  PctStatus authorize(const Pct* pct, const Hash256& actual_cart_hash,
                      std::int64_t actual_amount, std::uint64_t now_ns);

  std::uint64_t authorized() const noexcept { return authorized_; }
  std::uint64_t refused()    const noexcept { return refused_; }

private:
  const Signer&                  key_;
  std::unordered_set<std::uint64_t> burned_;
  std::uint64_t                  authorized_ = 0;
  std::uint64_t                  refused_    = 0;
};

}  // namespace rig
