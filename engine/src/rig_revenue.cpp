// rig-revenue: what the gateway EARNS a merchant, not just what it blocks.
// [Track 01 — "Grow the merchant's revenue"]
//
// Every other metric in this project is defensive: bypasses refused, anomalies caught.
// Those matter, but they answer the wrong question for a merchant, who wants to know
// what a control costs and what it returns.
//
// METHOD. The same seeded dataset the risk detector is evaluated on, run through THREE
// policies, so the comparison is like-for-like:
//
//   A · limit-only      what the rail does today: approve anything under the block
//   B · naive blocker   any risk signal DENIES -- the obvious way to build this
//   C · intent gateway  deterministic caps, and risk signals ESCALATE rather than block
//
// HONESTY. The dataset is synthetic -- there is no public corpus of agentic-payment
// fraud, the rails are months old. So these are the consequences of a stated behaviour
// model, not a measurement of real revenue. The model is in engine/src/dataset.cpp and
// the labelled set is written to CSV; argue with the assumptions, not with the
// arithmetic.
#include "rig/dataset.hpp"
#include "rig/risk.hpp"
#include <cstdio>
#include <algorithm>
#include <map>
#include <vector>

using namespace rig;

static const char* G = "\033[32m";
static const char* R = "\033[31m";
static const char* Y = "\033[33m";
static const char* D = "\033[2m";
static const char* B = "\033[1m";
static const char* Z = "\033[0m";

// A plausible mandate for a lunch-ordering agent: no single purchase above Rs 1,200,
// and only merchants 1-6 (the shops these agents actually use).
struct Mandate {
  std::int64_t  max_txn_paise = 120000;
  std::uint32_t max_merchant  = 6;
};

struct Ledger {
  std::int64_t fraud_through   = 0;   // anomalous value that completed
  std::int64_t fraud_stopped   = 0;   // anomalous value refused or held
  std::int64_t outside_mandate = 0;   // legitimate, but the human never authorised it
  std::int64_t killed_by_risk  = 0;   // legitimate, INSIDE the mandate, refused anyway
  std::int64_t good_completed  = 0;   // legitimate value that went through
  std::uint32_t prompts        = 0;   // confirmations shown to a human
  std::int64_t net() const { return good_completed - fraud_through; }
};

// "Outside the mandate" is not lost revenue -- the customer did not authorise it, and
// refusing it is the product working. Only a sale that was inside the mandate and got
// refused anyway is a loss, and that is the number the policies actually differ on.

static void row(const char* name, const Ledger& l, const char* note) {
  std::printf("  %s%-24s%s %s%11.0f%s %s%13.0f%s %s%12.0f%s %7u  %s%s%s\n",
    B, name, Z,
    R, l.fraud_through   / 100.0, Z,
    Y, l.killed_by_risk  / 100.0, Z,
    G, l.good_completed  / 100.0, Z,
    l.prompts, D, note, Z);
}

int main(int argc, char** argv) {
  const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 42;
  const std::uint32_t nses = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 400;
  const Dataset d = generate_dataset(seed, nses);
  const Mandate m;

  // The tuned operating point from rig-riskeval.
  RiskLimits lim;
  lim.flag_threshold = 3; lim.max_txn_in_burst = 2;
  lim.max_txn_in_window = 3; lim.max_spend_in_window = 100000;
  lim.merchant_min_txns = 8;

  Ledger limit_only, naive, naive_hi, gateway;

  std::map<std::uint64_t, std::vector<const Txn*>> by_session;
  for (const auto& t : d.txns) if (t.in_test) by_session[t.session_id].push_back(&t);

  // The comparison that matters. A team told to stop agentic fraud does not ship the
  // most conservative threshold -- they ship one that CATCHES things. At threshold 1 the
  // detector reaches 0.80 recall, which is what a risk owner would be asked for.
  RiskLimits lim_hi = lim; lim_hi.flag_threshold = 1;
  RiskEngine eng_naive(lim), eng_naive_hi(lim_hi), eng_gate(lim);

  for (auto& [sid, list] : by_session) {
    std::sort(list.begin(), list.end(),
              [](const Txn* a, const Txn* b){ return a->at_ns < b->at_ns; });
    AgentProfile pn{}; pn.agent_id = sid; pn.live = true;
    AgentProfile pnh{}; pnh.agent_id = sid; pnh.live = true;
    AgentProfile pg{}; pg.agent_id = sid; pg.live = true;

    for (const Txn* t : list) {
      const std::int64_t v = t->amount_paise;

      // ---- deterministic layer: the same in B and C, absent in A ----
      const bool breaks_mandate = v > m.max_txn_paise || t->merchant_id > m.max_merchant;

      // ---- A · limit-only: the rail today. One cap, nothing else. ----
      if (v > 1000000) { limit_only.fraud_stopped += t->anomalous ? v : 0; }
      else if (t->anomalous) limit_only.fraud_through += v;
      else                   limit_only.good_completed += v;

      // ---- B · naive blocker: deterministic caps + DENY on any risk signal ----
      {
        const std::uint32_t bits =
          eng_naive.assess(pn, t->merchant_id, v, t->at_ns, t->local_hour);
        const bool blocked = breaks_mandate || bits != 0;
        if (blocked) {
          if (t->anomalous)          naive.fraud_stopped += v;
          else if (breaks_mandate)   naive.outside_mandate += v;
          else                       naive.killed_by_risk  += v;   // a real sale, refused
        } else {
          if (t->anomalous) naive.fraud_through  += v;
          else              naive.good_completed += v;
          eng_naive.observe(pn, t->merchant_id, v, t->at_ns, t->local_hour);
        }
      }

      // ---- B-hi · the same naive design, tuned for catch rate ----
      {
        const std::uint32_t bits =
          eng_naive_hi.assess(pnh, t->merchant_id, v, t->at_ns, t->local_hour);
        const bool blocked = breaks_mandate || bits != 0;
        if (blocked) {
          if (t->anomalous)        naive_hi.fraud_stopped   += v;
          else if (breaks_mandate) naive_hi.outside_mandate += v;
          else                     naive_hi.killed_by_risk  += v;
        } else {
          if (t->anomalous) naive_hi.fraud_through  += v;
          else              naive_hi.good_completed += v;
          eng_naive_hi.observe(pnh, t->merchant_id, v, t->at_ns, t->local_hour);
        }
      }

      // ---- C · intent gateway: caps DENY, risk signals ESCALATE ----
      {
        const std::uint32_t bits =
          eng_gate.assess(pg, t->merchant_id, v, t->at_ns, t->local_hour);
        if (breaks_mandate) {                       // deterministic refusal
          if (t->anomalous) gateway.fraud_stopped   += v;
          else              gateway.outside_mandate += v;
        } else if (bits != 0) {                     // ask the human
          ++gateway.prompts;
          // A person approves their own purchase and declines one they did not make.
          // This is the assumption the whole comparison rests on, so it is stated
          // rather than buried: step-up converts a block into a question.
          if (t->anomalous) gateway.fraud_stopped += v;
          else {
            gateway.good_completed += v;            // approved, sale kept
            eng_gate.observe(pg, t->merchant_id, v, t->at_ns, t->local_hour);
          }
        } else {
          if (t->anomalous) gateway.fraud_through  += v;
          else              gateway.good_completed += v;
          eng_gate.observe(pg, t->merchant_id, v, t->at_ns, t->local_hour);
        }
      }
    }
  }

  std::printf("\n  %sRevenue impact%s  %sseed %llu · %u sessions · held-out split only%s\n",
              B, Z, D, (unsigned long long)seed, nses, Z);
  std::printf("  %sthree policies, identical traffic, rupees%s\n\n", D, Z);
  std::printf("  %s%-24s %11s %13s %12s %7s%s\n",
              D, "policy", "fraud thru", "sales killed", "revenue kept", "prompts", Z);

  row("A · limit-only",           limit_only, "the rail today");
  row("B · naive blocker",        naive,      "denies on any signal");
  row("B+ · naive, tuned to catch", naive_hi, "0.80 recall setting");
  row("C · intent gateway",       gateway,    "signals escalate");

  const double preserved = (gateway.good_completed - naive_hi.good_completed) / 100.0;
  const double prevented = (limit_only.fraud_through - gateway.fraud_through) / 100.0;
  const double uplift    = naive_hi.good_completed
                         ? 100.0 * (gateway.good_completed - naive_hi.good_completed)
                           / naive_hi.good_completed : 0.0;

  std::printf("\n  %sWhat the gateway is worth%s\n", B, Z);
  std::printf("    fraud prevented vs the rail today       %s+Rs %.0f%s\n", G, prevented, Z);
  std::printf("    revenue preserved vs a blocker at 0.80 %s+Rs %.0f%s  (%+.1f%%)\n",
              G, preserved, Z, uplift);
  std::printf("    friction to achieve it                  %u confirmations across %u\n",
              gateway.prompts, d.test_txns);
  std::printf("                                            legitimate purchases\n");

  std::printf("\n  %sThe trade being made%s\n", B, Z);
  std::printf("    Nobody ships the timid setting. A risk owner asked to stop agentic\n");
  std::printf("    fraud ships one that CATCHES -- and at 0.80 recall a blocker destroys\n");
  std::printf("    %sRs %.0f%s of sales that were inside the mandate all along.\n",
              R, naive_hi.killed_by_risk / 100.0, Z);
  std::printf("    This gateway kills %sRs %.0f%s, because an uncertain signal becomes a\n",
              G, gateway.killed_by_risk / 100.0, Z);
  std::printf("    question rather than a decline.\n");

  // The comparison has to be net, or it is just picking the favourable column.
  const double extra_fraud_caught =
      (gateway.fraud_through - naive_hi.fraud_through) / 100.0;
  const double sales_cost = (naive_hi.killed_by_risk - gateway.killed_by_risk) / 100.0;
  std::printf("\n    Netting it out honestly: the aggressive blocker DOES prevent\n");
  std::printf("    %sRs %.0f%s more fraud than this gateway -- and spends %sRs %.0f%s of real\n",
              Y, extra_fraud_caught, Z, R, sales_cost, Z);
  std::printf("    sales to do it. Net position: %s%s Rs %.0f%s for the merchant.\n",
              (sales_cost - extra_fraud_caught) > 0 ? G : R,
              (sales_cost - extra_fraud_caught) > 0 ? "gateway ahead by" : "gateway behind by",
              (sales_cost - extra_fraud_caught) > 0 ? (sales_cost - extra_fraud_caught)
                                                    : (extra_fraud_caught - sales_cost), Z);

  std::printf("\n  %sAssumptions, stated%s\n", D, Z);
  std::printf("    · synthetic traffic from engine/src/dataset.cpp; no public corpus of\n");
  std::printf("      agentic-payment fraud exists yet. Argue with the model, not the sums.\n");
  std::printf("    · a step-up prompt is answered correctly: people approve their own\n");
  std::printf("      purchases and decline ones they did not make.\n");
  std::printf("    · mandate modelled as Rs %.0f per purchase, merchants 1-%u.\n\n",
              m.max_txn_paise / 100.0, m.max_merchant);
  return 0;
}
