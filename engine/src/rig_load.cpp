// rig-load: substantiates the group-commit claim with a measurement, not an assertion.
#include "rig/gateway.hpp"
#include "rig/clock.hpp"
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <time.h>
#include <unistd.h>

using namespace rig;
static std::string slurp(const char* p){ std::ifstream f(p); std::ostringstream o; o<<f.rdbuf(); return o.str(); }
static inline uint64_t mono(){ return rig::mono_ns(); }

static int run(int argc, char** argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 2000;
  const char* wal = "wal/load.wal";
  ::unlink(wal);
  Gateway gw(wal);
  std::string err;
  UserDevice device("user_phone_9f21");
  gw.enroll_device(device.public_key(), device.label());
  const std::string intent = slurp("fixtures/lunch_intent.json");
  if (!gw.admit_mandate(intent, device.sign(intent), device.public_key(), err))
    throw std::runtime_error("admission: " + err);

  const std::string cart = slurp("fixtures/lunch_cart.json");
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  const std::uint64_t now = std::uint64_t(ts.tv_sec)*1000000000ull + ts.tv_nsec;

  std::printf("  NOTE: each decision writes 3 WAL records (CART_PROPOSED,\n"
              "        POLICY_DECISION, CAPABILITY_*), so a 256-record batch holds\n"
              "        ~85 decisions -- and the 2ms timer usually closes it sooner.\n\n");
  for (int mode = 0; mode < 2; ++mode) {
    const bool group = mode == 1;
    gw.set_group_commit(group);
    const int iters = group ? n : std::min(n, 60);   // un-amortised is ~4ms/txn, keep it short
    const std::uint64_t s0 = gw.wal().syncs(), u0 = gw.wal().sync_us();
    const std::uint64_t t0 = mono();
    for (int i = 0; i < iters; ++i) gw.decide(cart, now);
    gw.flush();
    const std::uint64_t dt = mono() - t0;
    const std::uint64_t ds = gw.wal().syncs() - s0, du = gw.wal().sync_us() - u0;
    std::printf("  %-24s %6d decisions in %8.1f ms -> %8.1f us/decision (%7.0f/s)\n",
      group ? "group commit (256/2ms)" : "commit every record",
      iters, dt/1e6, dt/1000.0/iters, iters/(dt/1e9));
    std::printf("  %-24s %6llu fsyncs, %6.2f ms in F_FULLFSYNC, %5.1f decisions/fsync\n\n",
      "", (unsigned long long)ds, du/1000.0, ds ? double(iters)/ds : 0.0);
  }
  ::unlink(wal);
  return 0;
}
int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::fprintf(stderr, "\n  error: %s\n\n", e.what()); return 4; }
}
