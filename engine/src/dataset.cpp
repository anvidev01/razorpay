#include "rig/dataset.hpp"
#include <cstdio>
#include <cstring>

namespace rig {

namespace {
// splitmix64: small, fast, and fully specified, so "seed 42" means the same thing
// everywhere. No dependence on the platform's rand().
struct Rng {
  std::uint64_t s;
  std::uint64_t next() {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  std::uint32_t below(std::uint32_t n) { return static_cast<std::uint32_t>(next() % n); }
  double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
  bool chance(double p) { return unit() < p; }
};

constexpr std::uint64_t DAY_NS  = 86400ull * 1000000000ull;
constexpr std::uint64_t HOUR_NS = 3600ull * 1000000000ull;
constexpr std::uint64_t MIN_NS  = 60ull * 1000000000ull;
constexpr std::uint64_t T0      = 1'700'000'000ull * 1000000000ull;
}  // namespace

Dataset generate_dataset(std::uint64_t seed, std::uint32_t n_sessions) {
  Dataset d;
  Rng rng{seed};
  d.sessions = n_sessions;

  for (std::uint32_t s = 0; s < n_sessions; ++s) {
    const std::uint64_t sid = 1000 + s;
    // Split by SESSION, not by transaction: a session's history is what the detector
    // scores against, so splitting mid-session would leak the answer.
    const bool in_test = ((sid * 2654435761ull) >> 32) % 100 < 40;   // ~40% held out

    // Each agent has its own habits: which merchants, what hours, how much.
    const std::uint32_t home_a = 1 + rng.below(6);
    const std::uint32_t home_b = 1 + rng.below(6);
    const int    peak_hour = 12 + static_cast<int>(rng.below(3));      // 12-14
    const std::int64_t typical = 15000 + rng.below(45000);             // Rs 150-600
    const std::uint32_t days   = 12 + rng.below(10);

    // --- ordinary weeks of behaviour ---
    for (std::uint32_t day = 0; day < days; ++day) {
      const std::uint32_t per_day = rng.chance(0.25) ? 2 : 1;
      for (std::uint32_t k = 0; k < per_day; ++k) {
        int hour = peak_hour + static_cast<int>(rng.below(3)) - 1;
        std::uint64_t at = T0 + day * DAY_NS + std::uint64_t(hour) * HOUR_NS
                         + rng.below(60) * MIN_NS;
        std::int64_t amt = typical + static_cast<std::int64_t>(rng.below(12000)) - 6000;
        std::uint32_t m = rng.chance(0.5) ? home_a : home_b;
        const char* lab = "normal";

        // HARD NEGATIVES -- legitimate but unusual. Without these the normal class is
        // trivially separable and the false-positive rate is a fiction.
        if (rng.chance(0.05)) { hour = peak_hour + 3; lab = "legit: late lunch"; }
        if (rng.chance(0.04)) { amt = typical * 2 + 20000; lab = "legit: team order"; }
        if (rng.chance(0.03)) { m = 1 + rng.below(6); lab = "legit: occasional merchant"; }
        at = T0 + day * DAY_NS + std::uint64_t(hour) * HOUR_NS + rng.below(60) * MIN_NS;
        d.txns.push_back({sid, at, m, amt, hour, false, lab, in_test});
      }
    }

    // --- one anomaly class per compromised session (about a third of sessions) ---
    if (rng.chance(0.34)) {
      const std::uint64_t base = T0 + std::uint64_t(days) * DAY_NS;
      switch (rng.below(4)) {
        case 0: {   // hijacked agent draining in a burst
          for (int i = 0; i < 6; ++i)
            d.txns.push_back({sid, base + std::uint64_t(i) * 2 * MIN_NS,
                              home_a, typical, peak_hour, true, "burst", in_test});
          break;
        }
        case 1: {   // exfiltration to a merchant this agent has never used
          for (int i = 0; i < 3; ++i)
            d.txns.push_back({sid, base + std::uint64_t(i) * 3 * HOUR_NS,
                              40 + rng.below(20), typical * 2, peak_hour, true,
                              "new merchant", in_test});
          break;
        }
        case 2: {   // activity at an hour this agent never operates
          for (int i = 0; i < 2; ++i)
            d.txns.push_back({sid, base + std::uint64_t(i) * DAY_NS + 3 * HOUR_NS,
                              home_a, typical, 3, true, "odd hour", in_test});
          break;
        }
        default: {  // spend escalation inside the normal window
          for (int i = 1; i <= 3; ++i)
            d.txns.push_back({sid, base + std::uint64_t(i) * 20 * MIN_NS,
                              home_a, typical * (2 + i), peak_hour, true,
                              "spend spike", in_test});
          break;
        }
      }
    }
  }

  for (const auto& t : d.txns) {
    if (t.in_test) { ++d.test_txns;  if (t.anomalous) ++d.test_anom; }
    else           { ++d.train_txns; if (t.anomalous) ++d.train_anom; }
  }
  return d;
}

bool write_dataset_csv(const Dataset& d, const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  std::fprintf(f, "session_id,at_ns,merchant_id,amount_paise,local_hour,label,anomalous,split\n");
  for (const auto& t : d.txns)
    std::fprintf(f, "%llu,%llu,%u,%lld,%d,%s,%d,%s\n",
                 (unsigned long long)t.session_id, (unsigned long long)t.at_ns,
                 t.merchant_id, (long long)t.amount_paise, t.local_hour,
                 t.label, t.anomalous ? 1 : 0, t.in_test ? "test" : "train");
  std::fclose(f);
  return true;
}

}  // namespace rig
