#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static inline uint64_t ticks(){ return mach_absolute_time(); }

// ---- verdict bits ----
enum : uint32_t {
  R_OK=0, R_SKU_NOT_IN_INTENT=1u<<0, R_QTY=1u<<1, R_UNIT_PRICE=1u<<2,
  R_CART_TOTAL=1u<<3, R_MERCHANT=1u<<4, R_EXPIRED=1u<<5, R_OVERFLOW=1u<<6
};

// ---- mandate: cache-aligned, POD, inline constraints ----
static constexpr int MAXC = 16;
struct alignas(128) IntentSchema {
  uint64_t mandate_id;
  uint64_t not_after_ns;
  int64_t  total_budget_paise;
  uint64_t merchant_allow_mask;     // 64 merchants
  uint64_t allow_mask;              // membership over dense local ids
  uint32_t n_constraints;
  uint32_t _pad0;
  uint32_t sku_id[MAXC];            // global interned ids, sorted
  int64_t  max_unit_paise[MAXC];
  uint32_t max_qty[MAXC];
};

// ---- cart: SoA, arena-backed ----
struct CartView {
  const uint32_t* __restrict sku_id;
  const int64_t*  __restrict unit_paise;
  const uint32_t* __restrict qty;
  uint32_t n;
  uint32_t merchant_local_id;
};

// branch-free lookup: constraint index for sku, or -1. MAXC is small+contiguous.
static inline int find_constraint(const IntentSchema& s, uint32_t sku) noexcept {
  int found = -1;
  for (int i = 0; i < MAXC; ++i) {
    int hit = (i < (int)s.n_constraints) & (s.sku_id[i] == sku);
    found = (hit ? i : found);   // cmov
  }
  return found;
}

__attribute__((noinline))
static uint32_t evaluate(const IntentSchema& s, const CartView& c, uint64_t now_ns) noexcept {
  uint32_t v = 0;
  v |= (uint32_t)(-(int)(now_ns > s.not_after_ns)) & R_EXPIRED;
  v |= (uint32_t)(-(int)(((s.merchant_allow_mask >> c.merchant_local_id) & 1ull) == 0)) & R_MERCHANT;

  int64_t total = 0;
  for (uint32_t i = 0; i < c.n; ++i) {
    const uint32_t sku = c.sku_id[i];
    const int64_t  up  = c.unit_paise[i];
    const uint32_t q   = c.qty[i];

    int ci = find_constraint(s, sku);
    v |= (uint32_t)(-(int)(ci < 0)) & R_SKU_NOT_IN_INTENT;

    int safe = ci < 0 ? 0 : ci;                       // clamp, no branch on data
    v |= (uint32_t)(-(int)(ci >= 0 && q > s.max_qty[safe])) & R_QTY;
    v |= (uint32_t)(-(int)(ci >= 0 && up > s.max_unit_paise[safe])) & R_UNIT_PRICE;

    int64_t line = 0;
    bool of1 = __builtin_mul_overflow(up, (int64_t)q, &line);
    bool of2 = __builtin_add_overflow(total, line, &total);
    v |= (uint32_t)(-(int)(of1 | of2)) & R_OVERFLOW;
  }
  v |= (uint32_t)(-(int)(total > s.total_budget_paise)) & R_CART_TOTAL;
  return v;
}

int main(int argc, char** argv){
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const double t2ns = (double)tb.numer/tb.denom;

  int NLINES = argc>1 ? atoi(argv[1]) : 8;

  alignas(128) IntentSchema s{};
  s.mandate_id=1; s.not_after_ns=UINT64_MAX; s.total_budget_paise=50000;
  s.merchant_allow_mask=0xF; s.n_constraints=MAXC;
  for(int i=0;i<MAXC;i++){ s.sku_id[i]=1000+i; s.max_unit_paise[i]=30000; s.max_qty[i]=5; }

  std::vector<uint32_t> sku(NLINES), qty(NLINES);
  std::vector<int64_t>  up(NLINES);
  for(int i=0;i<NLINES;i++){ sku[i]=1000+(i%MAXC); up[i]=200+i; qty[i]=1; }
  CartView c{sku.data(), up.data(), qty.data(), (uint32_t)NLINES, 1};

  // warm
  uint32_t sink=0;
  for(int i=0;i<200000;i++) sink ^= evaluate(s,c,1);

  // batched timing: N per batch to defeat 41.7ns timer quantisation
  const int BATCH=1000, ROUNDS=20000;
  std::vector<double> per; per.reserve(ROUNDS);
  for(int r=0;r<ROUNDS;r++){
    uint64_t t0=ticks();
    for(int i=0;i<BATCH;i++){ sink ^= evaluate(s,c,1); asm volatile("" :: "r"(sink) : "memory"); }
    uint64_t t1=ticks();
    per.push_back(((t1-t0)*t2ns)/BATCH);
  }
  std::sort(per.begin(), per.end());
  printf("cart_lines=%2d  kernel: p50=%7.1f ns  p99=%7.1f ns  p99.9=%7.1f ns  min=%6.1f ns  (sink=%u)\n",
    NLINES, per[ROUNDS/2], per[(size_t)(ROUNDS*0.99)], per[(size_t)(ROUNDS*0.999)], per.front(), sink);
  return 0;
}
