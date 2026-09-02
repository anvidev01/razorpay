// Agent-aware behavioural baselines.  [Track 02 — AI Risk Manager]
//
// Humans and agents have different signatures. A human buys lunch once a day from three
// or four merchants. A compromised or malfunctioning agent transacts in bursts, at odd
// hours, at merchants it has never used. Legacy velocity rules are tuned for humans and
// miss this entirely.
//
// DESIGN RULE, and it is the whole Track-02 argument: these signals produce REVIEW,
// never DENY. A behavioural score is probabilistic; auto-blocking on it converts a
// false positive into a lost sale and an angry customer. Here a false positive costs
// exactly one confirmation tap, and that cost is measured in docs/07-RISK-METRICS.md.
#pragma once
#include "schema.hpp"
#include <cstdint>
#include <cstring>

namespace rig {

inline constexpr std::uint32_t MAX_AGENTS = 256;

// Rolling per-agent baseline. Fixed size, no allocation, same discipline as the kernel.
struct alignas(128) AgentProfile {
  std::uint64_t agent_id        = 0;
  std::uint64_t window_start_ns = 0;
  std::uint32_t txn_in_window   = 0;
  std::int64_t  spend_in_window = 0;
  std::uint64_t merchants_seen  = 0;   // bitset over interned merchant ids 1..64
  std::uint32_t hour_hist[24]   = {};  // observed activity by hour, local time
  std::uint32_t total_txns      = 0;
  std::int64_t  lifetime_spend  = 0;
  std::uint64_t recent_ns[8]    = {};   // ring of recent completions, for burst detection
  std::uint8_t  recent_head     = 0;
  bool          live            = false;
};

struct RiskLimits {
  std::uint64_t window_ns          = 3600ull * 1000000000ull;  // 1 hour
  std::uint32_t max_txn_in_window  = 4;      // a lunch agent does not need more than this
  std::int64_t  max_spend_in_window= 150000; // Rs 1,500/hour before we ask the human
  std::uint32_t warmup_txns        = 3;      // no baseline yet => no opinion
  // Burst is the distinctly AGENT-shaped signal. A human cannot buy four times in ten
  // minutes; a looping or hijacked agent does exactly that. Legacy velocity rules are
  // tuned to human cadence and miss it entirely.
  std::uint64_t burst_window_ns    = 600ull * 1000000000ull;   // 10 minutes
  std::uint32_t max_txn_in_burst   = 3;
  // Hour tolerance: flag only hours genuinely far from anything observed, so a lunch
  // at 15:00 instead of 13:00 is not treated as suspicious.
  int           hour_tolerance     = 2;
};

class RiskEngine {
public:
  explicit RiskEngine(RiskLimits lim = {}) : lim_(lim) {}

  // Returns risk bits. Pure w.r.t. the profile it is given; the caller commits the
  // update only after the decision, so a denied cart never poisons the baseline.
  std::uint32_t assess(const AgentProfile& p, std::uint32_t merchant_id,
                       std::int64_t amount_paise, std::uint64_t now_ns,
                       int local_hour) const noexcept {
    std::uint32_t bits = 0;
    // No baseline yet -> no opinion. Firing on the first transaction an agent ever
    // makes is the classic cold-start false positive.
    if (p.total_txns < lim_.warmup_txns) return 0;

    const bool in_window = (now_ns - p.window_start_ns) < lim_.window_ns;
    const std::uint32_t txns  = in_window ? p.txn_in_window   : 0;
    const std::int64_t  spend = in_window ? p.spend_in_window : 0;

    if (txns + 1 > lim_.max_txn_in_window)                    bits |= R_VELOCITY_ANOMALY;
    if (spend + amount_paise > lim_.max_spend_in_window)      bits |= R_VELOCITY_ANOMALY;
    if (merchant_id >= 1 && merchant_id <= 64 &&
        ((p.merchants_seen >> (merchant_id - 1)) & 1ull) == 0) bits |= R_NEW_MERCHANT;

    // burst: how many completions fall inside the short window
    std::uint32_t in_burst = 0;
    for (std::uint64_t ts : p.recent_ns)
      if (ts && now_ns >= ts && (now_ns - ts) < lim_.burst_window_ns) ++in_burst;
    if (in_burst + 1 > lim_.max_txn_in_burst) bits |= R_VELOCITY_ANOMALY;

    if (local_hour >= 0 && local_hour < 24) {
      bool near = false;
      for (int d = -lim_.hour_tolerance; d <= lim_.hour_tolerance && !near; ++d) {
        const int h = ((local_hour + d) % 24 + 24) % 24;
        if (p.hour_hist[h]) near = true;
      }
      if (!near) bits |= R_ODD_HOUR;
    }
    return bits;
  }

  // Commit only for transactions that actually completed.
  void observe(AgentProfile& p, std::uint32_t merchant_id, std::int64_t amount_paise,
               std::uint64_t now_ns, int local_hour) const noexcept {
    if ((now_ns - p.window_start_ns) >= lim_.window_ns) {
      p.window_start_ns = now_ns;
      p.txn_in_window   = 0;
      p.spend_in_window = 0;
    }
    ++p.txn_in_window;
    p.spend_in_window += amount_paise;
    if (merchant_id >= 1 && merchant_id <= 64) p.merchants_seen |= (1ull << (merchant_id - 1));
    if (local_hour >= 0 && local_hour < 24) ++p.hour_hist[local_hour];
    p.recent_ns[p.recent_head] = now_ns;
    p.recent_head = static_cast<std::uint8_t>((p.recent_head + 1) % 8);
    ++p.total_txns;
    p.lifetime_spend += amount_paise;
  }

  AgentProfile& profile(std::uint64_t agent_id) noexcept {
    for (std::uint32_t i = 0; i < MAX_AGENTS; ++i) {
      const std::uint32_t k = (static_cast<std::uint32_t>(agent_id) + i) % MAX_AGENTS;
      if (tab_[k].live && tab_[k].agent_id == agent_id) return tab_[k];
      if (!tab_[k].live) { tab_[k] = AgentProfile{}; tab_[k].agent_id = agent_id;
                           tab_[k].live = true; return tab_[k]; }
    }
    return tab_[0];   // saturated: reuse slot 0 rather than allocate
  }

  const RiskLimits& limits() const noexcept { return lim_; }

private:
  RiskLimits   lim_;
  AgentProfile tab_[MAX_AGENTS]{};
};

}  // namespace rig
