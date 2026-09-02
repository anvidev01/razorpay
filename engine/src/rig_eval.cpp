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
  bool json_only = false;
  std::string wal_path = "wal/rig.wal";
  for (int i = 3; i < argc; ++i) {
    if (std::string(argv[i]) == "--json") json_only = true;
    else if (std::string(argv[i]) == "--wal" && i + 1 < argc) wal_path = argv[++i];
  }

  Gateway gw(wal_path);
  std::string err;
  if (!gw.admit_mandate(slurp(argv[1]), err)) {
    std::fprintf(stderr, "mandate rejected at admission: %s\n", err.c_str());
    return 3;
  }

  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  const std::uint64_t now = std::uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;

  const Decision d = gw.decide(slurp(argv[2]), now);

  if (json_only) { std::printf("%s\n", gw.decision_json(d).c_str()); return d.verdict.allowed() ? 0 : 1; }

  const bool ok = d.verdict.allowed();
  std::printf("\n  %s%s%s  verdict = %s0x%04X%s   eval = %s%llu ns%s   wal_seq = %llu\n",
    ok ? C_GRN : C_RED, ok ? "ALLOW" : "DENY", C_RST,
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
      std::printf("\n");
    }
  }
  if (!ok) std::printf("\n  %srepair%s %s\n", C_YEL, C_RST, gw.repair_hint_json(d).c_str());
  if (d.has_pct)
    std::printf("\n  %scapability issued%s nonce=%llu exp=+60s bound to cart %s...\n",
      C_GRN, C_RST, (unsigned long long)d.pct.body.nonce, hex(d.cart_hash).substr(0,16).c_str());
  else
    std::printf("\n  %sno capability token minted -- this cart cannot reach the payment rail%s\n",
      C_DIM, C_RST);
  std::printf("\n");
  return ok ? 0 : 1;
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\n  error: %s\n\n", e.what());
    return 4;
  }
}
