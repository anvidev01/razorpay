// rig-riskeval: measures the behavioural detector on a LABELLED scenario set.
// [Track 02 — "Honest metrics including false-positive cost. Strictly defense-only."]
//
// A fraud control with no false-positive number is a marketing claim. This runs a
// labelled set through the real RiskEngine and prints the confusion matrix, plus the
// thing that actually matters commercially: what a false positive COSTS here.
#include "rig/risk.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

using namespace rig;

struct Case {
  const char*  label;
  bool         is_anomalous;    // ground truth
  std::uint32_t merchant;
  std::int64_t amount;
  int          hour;
  int          minutes_after_start;
};

int main() {
  const RiskLimits lim;
  RiskEngine eng(lim);
  const std::uint64_t T0 = 1'700'000'000ull * 1000000000ull;
  const std::uint64_t MIN = 60ull * 1000000000ull;

  // A week of ordinary behaviour for one agent: lunch on weekdays, two known
  // merchants, midday. This is what the baseline is built from.
  std::vector<Case> cases;
  for (int d = 0; d < 7; ++d)
    for (int k = 0; k < 2; ++k)
      cases.push_back({"normal lunch", false, static_cast<std::uint32_t>((k % 2) + 1),
                       40000, 13, d * 1440 + k * 60});

  // Genuinely anomalous behaviour we want caught.
  cases.push_back({"burst: 5 buys in 10 min",  true, 1, 40000, 13, 7 * 1440 + 1});
  cases.push_back({"burst: 5 buys in 10 min",  true, 1, 40000, 13, 7 * 1440 + 3});
  cases.push_back({"burst: 5 buys in 10 min",  true, 1, 40000, 13, 7 * 1440 + 5});
  cases.push_back({"burst: 5 buys in 10 min",  true, 1, 40000, 13, 7 * 1440 + 7});
  cases.push_back({"burst: 5 buys in 10 min",  true, 1, 40000, 13, 7 * 1440 + 9});
  cases.push_back({"3am purchase",             true, 1, 40000,  3, 8 * 1440});
  cases.push_back({"never-seen merchant",      true, 9, 40000, 13, 9 * 1440});
  cases.push_back({"spend spike Rs 1900",      true, 1, 190000, 13, 10 * 1440});

  // Legitimate-but-unusual traffic. These are the false positives we must count
  // honestly rather than quietly exclude.
  cases.push_back({"legit: slightly late lunch", false, 1, 40000, 15, 11 * 1440});
  cases.push_back({"legit: second lunch order",  false, 2, 35000, 13, 12 * 1440 + 30});

  std::uint32_t tp = 0, fp = 0, tn = 0, fn = 0;
  std::printf("\n  %-30s %-12s %-10s %s\n", "case", "truth", "flagged", "signals");
  std::printf("  %s\n", "-------------------------------------------------------------------");

  AgentProfile& p = eng.profile(4242);
  for (const auto& c : cases) {
    const std::uint64_t now = T0 + std::uint64_t(c.minutes_after_start) * MIN;
    const std::uint32_t bits = eng.assess(p, c.merchant, c.amount, now, c.hour);
    const bool flagged = bits != 0;

    if (c.is_anomalous &&  flagged) ++tp;
    if (c.is_anomalous && !flagged) ++fn;
    if (!c.is_anomalous &&  flagged) ++fp;
    if (!c.is_anomalous && !flagged) ++tn;

    std::string sig;
    if (bits & R_VELOCITY_ANOMALY) sig += "VELOCITY ";
    if (bits & R_NEW_MERCHANT)     sig += "NEW_MERCHANT ";
    if (bits & R_ODD_HOUR)         sig += "ODD_HOUR ";
    std::printf("  %-30s %-12s %-10s %s\n", c.label,
                c.is_anomalous ? "anomalous" : "normal",
                flagged ? "REVIEW" : "-", sig.c_str());

    // Only transactions that would have completed shape the baseline.
    if (!flagged) eng.observe(p, c.merchant, c.amount, now, c.hour);
  }

  const double prec   = tp + fp ? double(tp) / (tp + fp) : 0.0;
  const double recall = tp + fn ? double(tp) / (tp + fn) : 0.0;
  const double fpr    = fp + tn ? double(fp) / (fp + tn) : 0.0;

  std::printf("\n  confusion matrix\n");
  std::printf("    true positives  %2u    false negatives %2u\n", tp, fn);
  std::printf("    false positives %2u    true negatives  %2u\n", fp, tn);
  std::printf("\n    precision %.2f   recall %.2f   false-positive rate %.2f\n",
              prec, recall, fpr);

  std::printf("\n  %s\n", "cost of a false positive");
  std::printf("    A flagged transaction is REVIEW, never DENY. The customer gets one\n");
  std::printf("    confirmation prompt and the purchase completes. Cost = one tap,\n");
  std::printf("    not a declined payment and not a lost sale.\n");
  std::printf("    %u false positive(s) => %u extra confirmation prompt(s) across %zu txns.\n",
              fp, fp, cases.size());
  std::printf("\n  %s\n", "defense-only");
  std::printf("    This module blocks nothing on its own and takes no automated punitive\n");
  std::printf("    action. Its only output is: ask the human first.\n\n");
  return 0;
}
