// Write-Ahead Log: append-only, hash-chained, group-committed.
//
// THE ORDERING INVARIANT: a capability token is minted only AFTER its POLICY_DECISION
// record is durable. Therefore a payment that is not in this log is cryptographically
// unconstructible. See docs/02-AUDIT-WAL.md.
//
// Durability note: on macOS, fsync() does NOT flush the drive write cache.
// Only fcntl(fd, F_FULLFSYNC) does. Measured: fsync 33.5us vs F_FULLFSYNC 3960us.
// An fsync-based WAL here would make the audit guarantee a fiction, so we pay the
// 4ms and amortise it with group commit (256 records / 2ms -> ~15.5us per decision).
#pragma once
#include "schema.hpp"
#include "crypto.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace rig {

enum class RecType : std::uint8_t {
  MANDATE_ISSUED    = 1,
  CART_PROPOSED     = 2,
  POLICY_DECISION   = 3,
  CAPABILITY_ISSUED = 4,
  CAPABILITY_DENIED = 5,
  PAYMENT_ATTEMPTED = 6,
  PAYMENT_RESULT    = 7,
  REMEDIATION       = 8,
  ANCHOR            = 9,
  DUPLICATE_SUPPRESSED = 10,
  STEP_UP_REQUIRED  = 11,   // REVIEW: engine asked the human before moving money
  HUMAN_CONFIRMED   = 12,   // the human's out-of-band answer, bound to a decision
  REVERSAL_REQUESTED = 13,  // a refund was asked for, and by whom
  REVERSAL_RESULT    = 14,  // what the rail did about it
};
const char* rectype_name(RecType t) noexcept;

#pragma pack(push, 1)
struct RecHeader {
  std::uint32_t len;        // total record bytes, including both hashes
  std::uint32_t crc;        // crc32c over bytes [8, len-64)
  std::uint64_t seq;
  std::uint64_t wall_ns;
  std::uint64_t mono_ns;
  std::uint8_t  type;
  std::uint8_t  version;
  std::uint16_t flags;
};
#pragma pack(pop)
static_assert(sizeof(RecHeader) == 36, "WAL header must be exactly 36 bytes");

inline constexpr std::size_t REC_OVERHEAD = sizeof(RecHeader) + 64;  // + prev_hash + this_hash

// Everything evaluate() needs, recorded verbatim, so the auditor can re-execute the
// decision offline and prove the verdict. POD by construction -- no pointers.
struct DecisionPayload {
  IntentSchema  schema;
  std::uint64_t now_ns;              // the exact clock value passed to evaluate()
  std::uint32_t merchant_id;
  std::uint32_t n_lines;
  std::uint32_t sku_id[MAX_CART];
  std::int64_t  unit_paise[MAX_CART];
  std::uint32_t qty[MAX_CART];
  std::uint32_t category_id[MAX_CART];
  std::int64_t  recurring_paise[MAX_CART];
  std::uint32_t text_flags;
  std::uint32_t recorded_bits;       // what the engine decided at the time
  std::int64_t  recorded_total;
  std::uint64_t eval_ns;
  std::uint64_t cart_hash_lo;        // first 8 bytes of sha256(canonical cart)
  std::uint8_t  idem_key[32];        // semantic retry key, so dedupe survives restart
  std::uint64_t agent_session_id;    // which agent session proposed this
  std::uint32_t risk_bits;           // behavioural signals (Track 02), REVIEW not DENY
  std::uint8_t  outcome;             // Outcome: ALLOW / REVIEW / DENY
  std::uint8_t  _pad2[3];
};

class Wal {
public:
  explicit Wal(std::string path, std::uint32_t group_size = 256, std::uint64_t group_us = 2000);
  ~Wal();
  Wal(const Wal&) = delete;
  Wal& operator=(const Wal&) = delete;

  // Appends to the in-memory batch. NOT durable until commit() returns.
  std::uint64_t append(RecType t, const void* payload, std::size_t n);

  // Commits if the batch is full or the age threshold passed. Returns true if it synced.
  bool maybe_commit();
  // Forces F_FULLFSYNC. Returns microseconds spent.
  std::uint64_t commit();

  Hash256       head()     const noexcept { return head_; }
  std::uint64_t next_seq() const noexcept { return seq_; }
  std::uint64_t committed_seq() const noexcept { return committed_seq_; }
  std::uint64_t syncs()    const noexcept { return syncs_; }
  std::uint64_t sync_us()  const noexcept { return sync_us_total_; }

private:
  int           fd_ = -1;
  std::string   path_;
  Hash256       head_{};
  std::uint64_t seq_           = 1;
  std::uint64_t committed_seq_ = 0;
  std::vector<std::uint8_t> batch_;
  std::uint32_t pending_       = 0;
  std::uint64_t batch_opened_us_ = 0;
  std::uint32_t group_size_;
  std::uint64_t group_us_;
  std::uint64_t syncs_         = 0;
  std::uint64_t sync_us_total_ = 0;
};

// ---- reading / verification ----
struct WalRecord {
  RecHeader                 hdr;
  std::vector<std::uint8_t> payload;
  Hash256                   prev_hash;
  Hash256                   this_hash;
};

struct ChainReport {
  std::uint64_t records    = 0;
  bool          intact     = true;
  std::uint64_t break_seq  = 0;
  std::string   detail;
  // Recovery state: how many bytes form a verified prefix, and the chain head there.
  std::uint64_t good_bytes = 0;
  std::uint64_t last_seq   = 0;
  Hash256       head{};
};

// Streams records; cb returns false to stop. Verifies CRC and the hash chain.
ChainReport wal_scan(const std::string& path,
                     const std::function<bool(const WalRecord&)>& cb);

}  // namespace rig
