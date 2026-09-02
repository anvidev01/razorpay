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
#include <string>
#include <unordered_map>
#include <memory>

namespace rig {

struct Decision {
  bool          parsed        = false;
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

  // ADMISSION PATH (cold): Ed25519-verify once, then freeze the schema and tag it.
  bool admit_mandate(const std::string& intent_json, std::string& err);

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
  std::unordered_map<std::uint64_t, std::uint32_t> by_id_;
  std::uint8_t                 tag_key_[16]{};
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
