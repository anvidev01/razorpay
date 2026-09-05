// Idempotency for agent retries.
//
// THE PROBLEM: a human whose checkout freezes clicks the same button again, and the
// payment rail recognises the identical request. An agent handling a timeout does not
// replay bytes -- it RE-GENERATES the request. Different JSON key order, different
// whitespace, a re-worded note, a fresh client-side id. The rail sees a new order and
// charges twice. Retries are the normal control flow for an agent, not the exception.
//
// THE FIX: key on SEMANTICS, not syntax. The idempotency key is derived from the
// canonical interned cart -- {mandate, cart_hash, merchant, amount} -- so any
// re-generation of the same shopping cart collapses onto the same key no matter how
// the model chose to spell it this time.
#pragma once
#include "crypto.hpp"
#include <cstdint>
#include <cstring>

namespace rig {

struct IdemEntry {
  Hash256       key{};
  std::uint64_t decision_id = 0;
  std::uint64_t wal_seq     = 0;
  std::uint64_t expires_ns  = 0;
  std::uint32_t hits        = 0;      // how many duplicate attempts collapsed here
  bool          allowed     = false;  // the ORIGINAL outcome, replayed verbatim
  bool          live        = false;
};

class IdempotencyStore {
public:
  static constexpr std::uint32_t CAP     = 4096;          // power of two
  static constexpr std::uint64_t TTL_NS  = 15ull * 60 * 1000000000ull;  // 15 minutes

  // Semantic key. Deliberately NOT over the raw JSON.
  static Hash256 key_of(std::uint64_t mandate_id, const Hash256& cart_hash,
                        std::uint32_t merchant_id, std::int64_t amount_paise) noexcept {
    std::uint8_t buf[8 + 32 + 4 + 8];
    std::memcpy(buf,      &mandate_id,  8);
    std::memcpy(buf + 8,  cart_hash.data(), 32);
    std::memcpy(buf + 40, &merchant_id, 4);
    std::memcpy(buf + 44, &amount_paise, 8);
    return sha256(buf, sizeof buf);
  }

  // Returns the prior entry if this cart was already decided and is still in window.
  IdemEntry* find(const Hash256& k, std::uint64_t now_ns) noexcept {
    std::uint32_t i = slot(k);
    for (std::uint32_t probe = 0; probe < CAP; ++probe) {
      IdemEntry& e = tab_[i];
      if (!e.live) return nullptr;
      if (e.key == k) {
        // Window closed. Do NOT clear live_: this is an open-addressed table, and a
        // cleared slot terminates the probe chain, making every key placed after it by
        // linear probing unreachable. A later cart that collided into this bucket would
        // then miss its own duplicate check -- a DOUBLE CHARGE. The slot stays in the
        // chain and is recycled by insert() instead.
        if (now_ns > e.expires_ns) return nullptr;
        return &e;
      }
      i = (i + 1) & (CAP - 1);
    }
    return nullptr;
  }

  void insert(const Hash256& k, std::uint64_t decision_id, std::uint64_t wal_seq,
              bool allowed, std::uint64_t now_ns) noexcept {
    std::uint32_t i = slot(k);
    for (std::uint32_t probe = 0; probe < CAP; ++probe) {
      IdemEntry& e = tab_[i];
      // A slot is reusable if it was never used, holds this same key, or has aged out.
      // Recycling expired slots here is what keeps the probe chain intact -- find()
      // deliberately does not clear them.
      const bool reusable = !e.live || e.key == k || now_ns > e.expires_ns;
      if (reusable) {
        const bool fresh = !e.live;
        e.key = k; e.decision_id = decision_id; e.wal_seq = wal_seq;
        e.allowed = allowed; e.expires_ns = now_ns + TTL_NS; e.live = true;
        e.hits = 0;
        if (fresh) ++live_;
        return;
      }
      i = (i + 1) & (CAP - 1);
    }
    // Table full: fail closed at the caller rather than silently forgetting a charge.
  }

  std::uint32_t live() const noexcept { return live_; }

private:
  static std::uint32_t slot(const Hash256& k) noexcept {
    std::uint32_t h;
    std::memcpy(&h, k.data(), 4);
    return h & (CAP - 1);
  }
  IdemEntry     tab_[CAP]{};
  std::uint32_t live_ = 0;
};

}  // namespace rig
