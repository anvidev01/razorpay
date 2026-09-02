// Benchmarks the PRODUCTION kernel (rigcore), not a prototype.
// Apples-to-apples with bench_kernel.cpp so the cost of per-line attribution is visible.
#include "rig/kernel.hpp"
#include "rig/intern.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

using namespace rig;

int main(int argc, char** argv) {
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const double t2ns = double(tb.numer) / tb.denom;
  const int n = argc > 1 ? std::atoi(argv[1]) : 8;

  InternTable t;
  IntentSchema s{};
  s.schema_version = SCHEMA_VER;
  s.not_before_ns = 0; s.not_after_ns = ~0ull;
  s.total_budget_paise = 5000000;
  s.merchant_allow_mask = 1ull;
  s.n_constraints = MAXC;
  for (int i = 0; i < MAXC; ++i) {
    char b[32]; std::snprintf(b, sizeof b, "SKU_%03d", i);
    s.sku_id[i] = t.intern(b);
    s.max_unit_paise[i] = 300000; s.max_qty[i] = 8;
  }
  std::vector<std::uint32_t> sku(n), qty(n);
  std::vector<std::int64_t> up(n);
  for (int i = 0; i < n; ++i) { sku[i] = s.sku_id[i % MAXC]; up[i] = 2000 + i; qty[i] = 1; }
  const CartView c{sku.data(), up.data(), qty.data(), (std::uint32_t)n, 1};

  std::uint32_t sink = 0;
  for (int i = 0; i < 200000; ++i) sink ^= evaluate(s, c, 1).bits;

  const int BATCH = 1000, ROUNDS = 20000;
  std::vector<double> per; per.reserve(ROUNDS);
  for (int r = 0; r < ROUNDS; ++r) {
    const std::uint64_t t0 = mach_absolute_time();
    for (int i = 0; i < BATCH; ++i) {
      sink ^= evaluate(s, c, 1).bits;
      asm volatile("" :: "r"(sink) : "memory");
    }
    per.push_back(double(mach_absolute_time() - t0) * t2ns / BATCH);
  }
  std::sort(per.begin(), per.end());
  std::printf("cart_lines=%2d  production kernel: p50=%7.1f ns  p99=%7.1f ns  min=%6.1f ns  (sink=%u)\n",
              n, per[ROUNDS/2], per[(size_t)(ROUNDS*0.99)], per.front(), sink);
  return 0;
}
