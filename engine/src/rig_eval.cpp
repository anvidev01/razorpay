// rig-eval: intent.json + cart.json -> verdict. The Day-1 gate.
#include "rig/gateway.hpp"
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <time.h>

using namespace rig;

static std::string slurp(const char* p) {
  std::ifstream f(p);
  if (!f) { std::fprintf(stderr, "cannot read %s\n", p); std::exit(2); }
  std::ostringstream o; o << f.rdbuf(); return o.str();
}
static const char* C_RED = "\033[31m", *C_GRN = "\033[32m", *C_DIM = "\033[2m",
                  *C_BLD = "\033[1m", *C_RST = "\033[0m", *C_YEL = "\033[33m";

static int run(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
      "usage: rig-eval <intent.json> <cart.json> [--json] [--wal PATH]\n");
    return 2;
  }
  bool json_only = false, do_exec = false;
  int  confirm_as = -1;                       // -1 none, 1 approve, 0 decline
  std::string wal_path = "wal/rig.wal", human_ref = "mfa_device_9f21";
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if      (a == "--json")    json_only = true;
    else if (a == "--execute") do_exec   = true;
    else if (a == "--wal"     && i + 1 < argc) wal_path  = argv[++i];
    else if (a == "--ref"     && i + 1 < argc) human_ref = argv[++i];
    else if (a == "--confirm" && i + 1 < argc) {
      const std::string v = argv[++i];
      confirm_as = (v == "approve" || v == "yes") ? 1 : 0;
    }
  }

  Gateway gw(wal_path);
  // The user's phone. It holds the signing key; the gateway only ever verifies.
  UserDevice device("user_phone_9f21");
  gw.enroll_device(device.public_key(), device.label());

  const std::string intent = slurp(argv[1]);
  std::string err;
  if (!gw.admit_mandate(intent, device.sign(intent), device.public_key(), err)) {
    std::fprintf(stderr, "mandate rejected at admission: %s\n", err.c_str());
    return 3;
  }

  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  const std::uint64_t now = std::uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;

  const Decision d = gw.decide(slurp(argv[2]), now);

  if (json_only) { std::printf("%s\n", gw.decision_json(d).c_str()); return d.verdict.allowed() ? 0 : 1; }

  const bool ok     = d.outcome == Outcome::ALLOW;
  const bool review = d.outcome == Outcome::REVIEW;
  const char* oc    = review ? C_YEL : (ok ? C_GRN : C_RED);
  std::printf("\n  %s%s%s  verdict = %s0x%04X%s   eval = %s%llu ns%s   wal_seq = %llu\n",
    oc, outcome_name(d.outcome), C_RST,
    C_BLD, d.verdict.bits, C_RST, C_BLD, (unsigned long long)d.eval_ns, C_RST,
    (unsigned long long)d.wal_seq);
  std::printf("  %scart total%s %lld paise (Rs %.2f)   %sdurable in%s %llu us\n",
    C_DIM, C_RST, (long long)d.verdict.cart_total_paise,
    d.verdict.cart_total_paise / 100.0, C_DIM, C_RST, (unsigned long long)d.commit_us);
  // The in-flight eval_ns above is ONE sample on a 41.7ns-granular timer, so it is
  // noise-dominated. This is the honest, batched kernel cost.
  std::printf("  %skernel (batched, n=200k)%s %s%.1f ns%s %s<- the number in docs/BENCHMARKS.md%s\n",
    C_DIM, C_RST, C_BLD, gw.measure_last_kernel_ns(), C_RST, C_DIM, C_RST);
  if (!d.parse_error.empty())
    std::printf("  %serror%s %s\n", C_YEL, C_RST, d.parse_error.c_str());

  if (d.agent_session_id)
    std::printf("  %sagent session%s %llu   %srail%s %s\n", C_DIM, C_RST,
      (unsigned long long)d.agent_session_id, C_DIM, C_RST, gw.rail_name());
  if (!ok) {
    std::printf("\n  %sreasons (all of them -- the kernel never short-circuits)%s\n", C_DIM, C_RST);
    for (int b = 0; b < R_BIT_COUNT; ++b) {
      const std::uint32_t bit = 1u << b;
      if (!(d.verdict.bits & bit)) continue;
      std::printf("    %s|-%s %-24s %s%s%s\n", C_RED, C_RST, reject_name(bit),
                  C_DIM, reject_help(bit), C_RST);
    }
  }
  if (d.parsed && d.verdict.n_lines) {
    std::printf("\n  %sper-line attribution%s\n", C_DIM, C_RST);
    for (std::uint32_t i = 0; i < d.verdict.n_lines; ++i) {
      const auto& L = d.verdict.lines[i];
      const bool lok = L.bits == R_NONE;
      std::printf("    %s%s%s  %-26.*s", lok ? C_GRN : C_RED, lok ? "ok  " : "DENY", C_RST,
        (int)gw.intern().name(L.sku_id).size(), gw.intern().name(L.sku_id).data());
      if (!lok) {
        for (int b = 0; b < R_BIT_COUNT; ++b)
          if (L.bits & (1u << b)) std::printf(" %s", reject_name(1u << b));
      }
      if (L.substituted_for) {
        const auto orig = gw.intern().name(L.substituted_for);
        std::printf(" %ssubstituted for %.*s%s", C_YEL, (int)orig.size(), orig.data(), C_RST);
      }
      std::printf("\n");
    }
  }
  if (d.duplicate_suppressed)
    std::printf("\n  %sDUPLICATE SUPPRESSED%s this exact basket was already decided as #%llu"
                " (attempt %u)\n  %sno second charge -- the retry collapsed onto the original"
                " decision%s\n",
      C_YEL, C_RST, (unsigned long long)d.original_decision_id, d.duplicate_hits,
      C_DIM, C_RST);
  if (!ok) std::printf("\n  %srepair%s %s\n", C_YEL, C_RST, gw.repair_hint_json(d).c_str());
  if (d.duplicate_suppressed)
    std::printf("\n  %sno NEW token minted -- decision #%llu already stands, so the basket"
                " is charged exactly once%s\n",
      C_DIM, (unsigned long long)d.original_decision_id, C_RST);
  else if (!ok && !review)
    std::printf("\n  %sno capability token minted -- this cart cannot reach the payment rail%s\n",
      C_DIM, C_RST);
  Decision act = d;
  if (review) {
    std::printf("\n  %sSTEP-UP REQUIRED%s behavioural signal only -- the cart is within intent,\n"
                "  %sso the engine asks the human instead of killing a legitimate purchase.%s\n",
      C_YEL, C_RST, C_DIM, C_RST);
    if (confirm_as >= 0) {
      Decision c;
      if (gw.confirm(d.decision_id, confirm_as == 1, human_ref, now, c)) {
        act = c;
        std::printf("  %shuman answered%s %s  (ref %s)  -> %s%s%s\n", C_DIM, C_RST,
          confirm_as ? "APPROVE" : "DECLINE", human_ref.c_str(),
          confirm_as ? C_GRN : C_RED, outcome_name(c.outcome), C_RST);
      }
    } else {
      std::printf("  %sre-run with --confirm approve --ref <id> to complete%s\n", C_DIM, C_RST);
    }
  }

  if (act.has_pct)
    std::printf("\n  %scapability issued%s nonce=%llu bound to cart %s...\n",
      C_GRN, C_RST, (unsigned long long)act.pct.body.nonce, hex(act.cart_hash).substr(0,16).c_str());

  if (do_exec && act.has_pct) {
    const bool paid = gw.execute(act, now);
    std::printf("\n  %spayment%s rail=%s%s%s status=%ld %s%s%s%s\n",
      C_DIM, C_RST, C_BLD, act.payment.rail.c_str(), C_RST, act.payment.http_status,
      paid ? C_GRN : C_RED, paid ? "PAID " : "FAILED ", C_RST,
      paid ? act.payment.order_id.c_str() : act.payment.error.c_str());
  } else if (do_exec) {
    std::printf("\n  %sno capability token -- nothing to execute%s\n", C_DIM, C_RST);
  }

  std::printf("\n");
  return act.outcome == Outcome::ALLOW ? 0 : 1;
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\n  error: %s\n\n", e.what());
    return 4;
  }
}
