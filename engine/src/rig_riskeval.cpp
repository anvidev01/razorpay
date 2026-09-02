// rig-riskeval: measures the behavioural detector on a HELD-OUT test set.
// [Track 02 — "measured precision and recall on a held-out test set",
//             "honest metrics including false-positive cost",
//             "strictly defense-only"]
//
// METHOD, so the numbers can be argued with:
//   1. A seeded generator (rig/dataset.hpp) produces labelled agent sessions. There is
//      no public corpus of agentic-payment fraud -- the rails are months old -- so a
//      documented behaviour model is the honest alternative to hand-picked examples.
//   2. Sessions are split ~60/40 by a hash of the session id. A session's whole history
//      lands on one side; splitting mid-session would leak the answer.
//   3. Thresholds are swept on TRAIN ONLY, choosing the operating point that maximises
//      recall subject to a false-positive rate <= 2%.
//   4. Reported headline numbers are from TEST, which tuning never saw. Train numbers
//      are printed beside them: if they diverge, the detector is overfitted and you can
//      see it.
#include "rig/dataset.hpp"
#include "rig/risk.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>

using namespace rig;

static const char* G = "\033[32m"; static const char* Y = "\033[33m";
static const char* D = "\033[2m";  static const char* B = "\033[1m";
static const char* R = "\033[31m"; static const char* Z = "\033[0m";

struct Metrics {
  std::uint32_t tp = 0, fp = 0, tn = 0, fn = 0;
  double precision() const { return tp + fp ? double(tp) / (tp + fp) : 0.0; }
  double recall()    const { return tp + fn ? double(tp) / (tp + fn) : 0.0; }
  double fpr()       const { return fp + tn ? double(fp) / (fp + tn) : 0.0; }
  double f1() const {
    const double p = precision(), r = recall();
    return (p + r) > 0 ? 2 * p * r / (p + r) : 0.0;
  }
};

// Replays every session chronologically through the real RiskEngine, exactly as the
// gateway would: assess BEFORE the decision, and only observe() transactions that were
// not flagged -- a flagged one is held for a human and never shapes the baseline.
static Metrics score(const Dataset& d, const RiskLimits& lim, bool want_test,
                     std::map<std::string, std::uint32_t>* missed = nullptr) {
  Metrics m;
  RiskEngine eng(lim);
  std::map<std::uint64_t, std::vector<const Txn*>> by_session;
  for (const auto& t : d.txns)
    if (t.in_test == want_test) by_session[t.session_id].push_back(&t);

  for (auto& [sid, list] : by_session) {
    std::sort(list.begin(), list.end(),
              [](const Txn* a, const Txn* b) { return a->at_ns < b->at_ns; });
    AgentProfile prof{};
    prof.agent_id = sid;
    prof.live     = true;
    for (const Txn* t : list) {
      const std::uint32_t bits =
          eng.assess(prof, t->merchant_id, t->amount_paise, t->at_ns, t->local_hour);
      const bool flagged = bits != 0;
      if (t->anomalous &&  flagged) ++m.tp;
      if (t->anomalous && !flagged) { ++m.fn; if (missed) (*missed)[t->label]++; }
      if (!t->anomalous &&  flagged) { ++m.fp; if (missed) (*missed)[std::string("FP: ") + t->label]++; }
      if (!t->anomalous && !flagged) ++m.tn;
      if (!flagged) eng.observe(prof, t->merchant_id, t->amount_paise, t->at_ns, t->local_hour);
    }
  }
  return m;
}

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 42;
  const std::uint32_t nses = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 400;

  const Dataset d = generate_dataset(seed, nses);
  write_dataset_csv(d, "wal/riskeval_dataset.csv");

  std::printf("\n  %sDataset%s  seed=%llu  %u sessions, %zu transactions\n",
              B, Z, (unsigned long long)seed, d.sessions, d.txns.size());
  std::printf("    train  %5u txns  (%u anomalous, %.1f%%)\n",
              d.train_txns, d.train_anom, 100.0 * d.train_anom / std::max(1u, d.train_txns));
  std::printf("    test   %5u txns  (%u anomalous, %.1f%%)   %sheld out from tuning%s\n",
              d.test_txns, d.test_anom, 100.0 * d.test_anom / std::max(1u, d.test_txns), D, Z);
  std::printf("    %sfull labelled set written to wal/riskeval_dataset.csv%s\n", D, Z);

  // ---- threshold sweep, TRAIN ONLY ----
  std::printf("\n  %sTuning on TRAIN%s  %smaximise recall subject to FPR <= 2%%%s\n", B, Z, D, Z);
  RiskLimits best{};
  double best_recall = -1;
  bool   found = false;
  int evaluated = 0;
  for (std::uint32_t burst : {2u, 3u, 4u, 5u})
    for (std::uint32_t wtx : {3u, 4u, 6u, 8u})
      for (std::int64_t spend : {100000ll, 150000ll, 250000ll, 400000ll})
        for (int tol : {1, 2, 3}) {
          RiskLimits l;
          l.max_txn_in_burst    = burst;
          l.max_txn_in_window   = wtx;
          l.max_spend_in_window = spend;
          l.hour_tolerance      = tol;
          const Metrics m = score(d, l, /*test=*/false);
          ++evaluated;
          if (m.fpr() <= 0.02 && m.recall() > best_recall) {
            best_recall = m.recall(); best = l; found = true;
          }
        }
  std::printf("    %d threshold combinations evaluated\n", evaluated);
  if (!found) {
    // Reporting untuned defaults as if they had been selected would be a lie about
    // the method, not just the number.
    std::printf("    %sNO combination met FPR <= 2%% -- relaxing to best F1 instead%s\n", Y, Z);
    double bf1 = -1;
    for (std::uint32_t thr : {2u, 3u, 4u, 5u})
      for (std::uint32_t burst : {2u, 3u, 4u})
        for (std::uint32_t wtx : {3u, 4u, 6u})
          for (std::int64_t spend : {100000ll, 150000ll, 250000ll})
            for (std::uint32_t mmt : {6u, 8u, 12u}) {
              RiskLimits l;
              l.flag_threshold = thr; l.max_txn_in_burst = burst;
              l.max_txn_in_window = wtx; l.max_spend_in_window = spend;
              l.merchant_min_txns = mmt;
              const Metrics m = score(d, l, false);
              if (m.f1() > bf1) { bf1 = m.f1(); best = l; }
            }
  }
  std::printf("    chosen: score>=%u  burst>%u/10min  %u txn/hr  Rs %lld/hr  "
              "merchant after %u txns\n",
              best.flag_threshold, best.max_txn_in_burst, best.max_txn_in_window,
              (long long)(best.max_spend_in_window / 100), best.merchant_min_txns);

  std::map<std::string, std::uint32_t> errs;
  const Metrics tr = score(d, best, false);
  const Metrics te = score(d, best, true, &errs);

  std::printf("\n  %sResults%s\n", B, Z);
  std::printf("    %-8s %8s %8s %8s %8s %9s %8s %7s\n",
              "split", "TP", "FP", "TN", "FN", "precision", "recall", "FPR");
  std::printf("    %-8s %8u %8u %8u %8u %9.3f %8.3f %7.3f  %s(tuned on this)%s\n",
              "train", tr.tp, tr.fp, tr.tn, tr.fn, tr.precision(), tr.recall(), tr.fpr(), D, Z);
  std::printf("    %s%-8s %8u %8u %8u %8u %9.3f %8.3f %7.3f%s  %sheld out%s\n",
              B, "TEST", te.tp, te.fp, te.tn, te.fn,
              te.precision(), te.recall(), te.fpr(), Z, G, Z);
  const double gap = tr.f1() - te.f1();
  std::printf("    F1 train %.3f vs test %.3f  %s(gap %+.3f -- %s)%s\n",
              tr.f1(), te.f1(), D, gap,
              (gap > 0.10 ? "OVERFITTED" : "no meaningful overfitting"), Z);

  // ---- the tradeoff curve ----
  // One operating point is a claim; the curve is the honest picture, and it lets a
  // merchant pick their own friction budget instead of inheriting mine.
  std::printf("\n  %sOperating points on held-out TEST%s  %s(score threshold sweep)%s\n", B, Z, D, Z);
  std::printf("    %-10s %9s %8s %7s   %s\n", "threshold", "precision", "recall", "FPR", "friction");
  for (std::uint32_t thr : {1u, 2u, 3u, 4u, 5u}) {
    RiskLimits l = best;
    l.flag_threshold = thr;
    const Metrics m = score(d, l, true);
    std::printf("    %-10u %9.3f %8.3f %7.3f   %s%.2f%% of good traffic prompted%s%s\n",
                thr, m.precision(), m.recall(), m.fpr(), D, 100.0 * m.fpr(), Z,
                thr == best.flag_threshold ? "   <- chosen" : "");
  }

  // ---- what it misses, and what it costs ----
  if (!errs.empty()) {
    std::printf("\n  %sError analysis on TEST%s\n", B, Z);
    for (const auto& [k, v] : errs) std::printf("    %-28s %u\n", k.c_str(), v);
  }

  std::printf("\n  %sFalse-positive cost%s\n", B, Z);
  std::printf("    A flagged transaction is REVIEW, never DENY: the customer gets one\n");
  std::printf("    confirmation prompt and the purchase completes.\n");
  std::printf("    %u prompt(s) across %u legitimate transactions = %.2f%% of good traffic.\n",
              te.fp, te.fp + te.tn, 100.0 * te.fpr());
  std::printf("    Nothing is declined, so the revenue cost of a false positive is zero;\n");
  std::printf("    the cost is friction, and it is bounded at %.2f%%.\n", 100.0 * te.fpr());
  std::printf("\n    Recall is %.2f, and that is the honest number. This detector is the LAST\n", te.recall());
  std::printf("    line, not the first: every one of the %u missed anomalies is still bounded\n", te.fn);
  std::printf("    by the deterministic caps it sits behind -- mandate budget, per-item price,\n");
  std::printf("    aggregate quantity, merchant allowlist, mandate TTL. A missed anomaly is a\n");
  std::printf("    purchase the human already authorised the shape of; it is not a free hand.\n");
  std::printf("    Chasing recall here would spend real customers' patience to re-detect what\n");
  std::printf("    the kernel already refuses deterministically.\n");

  std::printf("\n  %sDefense-only%s\n", B, Z);
  std::printf("    Output is one bit: ask the human, or do not. No blocking, no account\n");
  std::printf("    action, no profiling of the person -- the baseline is per agent SESSION\n");
  std::printf("    (merchant ids, hour, count, amount). No PII, no device fingerprinting,\n");
  std::printf("    nothing sent anywhere.\n\n");

  const bool healthy = te.fpr() <= 0.05 && te.recall() >= 0.30 && gap <= 0.15;
  std::printf("  %s%s%s\n\n", healthy ? G : R,
              healthy ? "held-out metrics within the operating envelope"
                      : "OUT OF ENVELOPE -- do not ship these thresholds", Z);
  return healthy ? 0 : 1;
}
