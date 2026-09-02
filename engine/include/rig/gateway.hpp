// Orchestration: admit -> parse -> evaluate -> log -> (maybe) mint.
//
// The ordering invariant lives in decide(): the PCT is minted only after the
// POLICY_DECISION record is durable. Nothing else in the system may mint one.
#pragma once
#include "schema.hpp"
#include "kernel.hpp"
#include "intern.hpp"
#include "arena.hpp"
#include "parse.hpp"
#include "wal.hpp"
#include "pct.hpp"
#include "pool.hpp"
#include "idempotency.hpp"
#include "risk.hpp"
#include "rail.hpp"
#include <string>
#include <unordered_map>
#include <memory>

namespace rig {

// Human-bound authorisation for one decision. [Liability gap]
struct Confirmation {
  bool          present   = false;
  bool          approved  = false;
  std::uint64_t at_ns     = 0;
  char          human_ref[32]{};   // e.g. the MFA/device reference the human used
};

struct Decision {
  bool          parsed        = false;
  Outcome       outcome       = Outcome::DENY;
  std::uint32_t risk_bits     = 0;
  std::uint64_t agent_session_id = 0;
  std::uint32_t merchant_id     = 0;
  Confirmation  confirmation{};
  PaymentResult payment{};
  bool          paid          = false;
  Verdict       verdict{};
  std::uint64_t eval_ns       = 0;
  std::uint64_t decision_id   = 0;
  std::uint64_t mandate_id    = 0;
  std::uint64_t wal_seq       = 0;
  std::uint64_t commit_us     = 0;
  Hash256       cart_hash{};
  std::int64_t  amount_paise  = 0;
  bool          has_pct       = false;
  Pct           pct{};
  std::string   parse_error;
  // Agent-retry handling: a duplicate is not an error, it is the SAME decision.
  bool          duplicate_suppressed  = false;
  std::uint64_t original_decision_id  = 0;
  std::uint32_t duplicate_hits        = 0;
  Hash256       idem_key{};
};

class Gateway {
public:
  explicit Gateway(std::string wal_path);

  // Enrol the user's device public key. In production this happens once, out of band,
  // when the user links their UPI app -- the gateway receives a PUBLIC key and never
  // holds the private half.
  void enroll_device(const std::array<std::uint8_t, 32>& pub, std::string label);
  bool device_enrolled() const noexcept { return device_enrolled_; }
  std::string device_fingerprint() const { return hex(enrolled_pub_.data(), 8); }

  // ADMISSION PATH (cold). The mandate must carry a signature from the ENROLLED device
  // over its exact bytes. A mandate signed by any other key, or altered after signing,
  // is refused here and never reaches the policy engine.
  bool admit_mandate(const std::string& intent_json, const Sig512& sig,
                     const std::array<std::uint8_t, 32>& pub, std::string& err);

  // HOT PATH: ~270ns of compute, then the durable-commit fence.
  Decision decide(const std::string& cart_json, std::uint64_t now_ns);

  // Group commit. When enabled, decide() appends and lets the batch policy decide when
  // to F_FULLFSYNC (256 records / 2ms). The ORDERING INVARIANT still holds: a Decision
  // returned with has_pct==true has always passed the fence, because the token is only
  // minted once the batch containing its POLICY_DECISION record is durable. Decisions
  // whose batch has not yet closed come back pending -- call flush() to close it.
  void set_group_commit(bool on) noexcept { group_commit_ = on; }
  std::uint64_t flush() { return wal_->commit(); }

  // Re-runs the kernel on the LAST decision's inputs, batched, to defeat the 41.7ns
  // timer granularity on Apple Silicon. The in-flight eval_ns is a single shot and is
  // dominated by timer overhead + quantisation; this is the number that matches
  // docs/BENCHMARKS.md. Reporting both keeps the claim honest.
  double measure_last_kernel_ns(int batch = 1000, int rounds = 200) const;

  // REVIEW path: the human's out-of-band answer, cryptographically bound to the
  // decision it authorises. This is the artefact a chargeback turns on.
  bool confirm(std::uint64_t decision_id, bool approved, const std::string& human_ref,
               std::uint64_t now_ns, Decision& out);

  // Executes an authorised decision against the payment rail. Requires a valid PCT,
  // so there is no path from the agent to money that skips the engine.
  bool execute(Decision& d, std::uint64_t now_ns);

  const char*  rail_name() const noexcept { return rail_ ? rail_->name() : "none"; }
  RiskEngine&  risk() noexcept { return risk_; }

  // Explainability: render a decision as JSON for the audit UI / CLI.
  std::string decision_json(const Decision& d) const;
  std::string repair_hint_json(const Decision& d) const;

  InternTable&   intern()   noexcept { return intern_; }
  Wal&           wal()      noexcept { return *wal_; }
  Executor&      executor() noexcept { return exec_; }
  const Signer&  key()      const noexcept { return key_; }
  std::size_t    mandates() const noexcept { return by_id_.size(); }

private:
  std::string                  wal_path_;
  InternTable                  intern_;
  ScratchArena                 arena_{1 << 20};
  std::unique_ptr<Wal>         wal_;
  Signer                       key_;
  Executor                     exec_{key_};
  FixedPool<IntentSchema, 256> pool_;
  IdempotencyStore             idem_;
  RiskEngine                   risk_;
  std::unique_ptr<PaymentRail> rail_;
  std::string                  rail_name_;

  // Decisions parked awaiting a human answer.
  struct Pending {
    std::uint64_t decision_id, mandate_id, wal_seq;
    std::int64_t  amount_paise;
    std::uint32_t merchant_id;
    std::uint64_t agent_session_id;
    Hash256       cart_hash, rec_head;
    bool          live = false;
  };
  Pending pending_[64]{};
  std::unordered_map<std::uint64_t, std::uint32_t> by_id_;
  std::uint8_t                 tag_key_[16]{};
  std::array<std::uint8_t, 32> enrolled_pub_{};
  std::string                  enrolled_label_;
  bool                         device_enrolled_ = false;
  std::uint64_t                next_decision_ = 1;
  std::uint64_t                next_nonce_    = 1;
  bool                         group_commit_  = false;

  Tag128 tag_of(const IntentSchema& s) const noexcept;
  void   rebuild_idempotency();

  // snapshot of the last decision's inputs, for honest re-measurement
  IntentSchema  last_schema_{};
  std::uint32_t last_sku_[MAX_CART]{};
  std::int64_t  last_up_[MAX_CART]{};
  std::uint32_t last_qty_[MAX_CART]{};
  std::uint32_t last_cat_[MAX_CART]{};
  std::uint32_t last_n_        = 0;
  std::uint32_t last_merchant_ = 0;
  std::uint32_t last_flags_    = 0;
  std::uint64_t last_now_      = 0;
};

}  // namespace rig
