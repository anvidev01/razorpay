// Golden-vector tests for the deterministic policy kernel.
#include "rig/kernel.hpp"
#include "rig/intern.hpp"
#include "rig/arena.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rig;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, msg) do{ ++g_run; if(!(cond)){ ++g_fail; \
  std::printf("  FAIL %-46s (%s:%d)\n", (msg), __FILE__, __LINE__);} }while(0)

static IntentSchema lunch_mandate(InternTable& t) {
  IntentSchema s{};
  s.mandate_id         = 0x8f21c4;
  s.not_before_ns      = 1000;
  s.not_after_ns       = 9'000'000'000ull;
  s.total_budget_paise = 50000;                 // Rs 500
  s.schema_version     = SCHEMA_VER;
  const std::uint32_t swiggy = t.intern("swiggy");
  s.merchant_allow_mask = 1ull << (swiggy - 1);
  struct C { const char* sku; std::int64_t up; std::uint32_t q; } cs[] = {
    {"SKU_MEAL_THALI_001",  30000, 2},
    {"SKU_MEAL_BIRYANI_02", 35000, 2},
    {"SKU_DRINK_LIME_007",   8000, 4},
    {"SKU_SIDE_RAITA_014",   6000, 2},
  };
  s.n_constraints = 4;
  for (int i = 0; i < 4; ++i) {
    s.sku_id[i]         = t.intern(cs[i].sku);
    s.max_unit_paise[i] = cs[i].up;
    s.max_qty[i]        = cs[i].q;
  }
  return s;
}

struct Cart {
  std::vector<std::uint32_t> sku, qty;
  std::vector<std::int64_t>  up;
  CartView view(std::uint32_t merchant) {
    return CartView{sku.data(), up.data(), qty.data(), (std::uint32_t)sku.size(), merchant};
  }
  void add(std::uint32_t s, std::int64_t u, std::uint32_t q){ sku.push_back(s); up.push_back(u); qty.push_back(q); }
};

int main() {
  InternTable t;
  IntentSchema s = lunch_mandate(t);
  const std::uint32_t swiggy = t.lookup("swiggy");
  const std::uint64_t NOW = 5'000'000'000ull;

  std::printf("== kernel golden vectors ==\n");

  {  // the happy path
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 1);
            c.add(t.lookup("SKU_DRINK_LIME_007"),  6000, 2);
    auto v = evaluate(s, c.view(swiggy), NOW);
    CHECK(v.allowed(),                    "lunch cart is ALLOWED");
    CHECK(v.bits == R_NONE,                 "lunch verdict == 0");
    CHECK(v.cart_total_paise == 36000,    "lunch total = 36000 paise");
  }

  {  // THE BLENDER -- the demo case
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 1);
            c.add(t.lookup("SKU_DRINK_LIME_007"),  6000, 2);
            c.add(t.intern("SKU_APPLIANCE_BLENDER_5"), 600000, 1);
    auto v = evaluate(s, c.view(swiggy), NOW);
    CHECK(!v.allowed(),                                  "blender cart is DENIED");
    CHECK(v.bits & R_SKU_NOT_IN_INTENT,                  "blender: not in intent");
    CHECK(v.bits & R_UNIT_PRICE_EXCEEDED,                "blender: unit price exceeded");
    CHECK(v.bits & R_CART_TOTAL_EXCEEDED,                "blender: cart total exceeded");
    CHECK(v.bits == 0x000D,                              "blender verdict == 0x000D");
    CHECK(v.lines[0].bits == R_NONE,                       "line 0 (thali) clean");
    CHECK(v.lines[2].bits & R_SKU_NOT_IN_INTENT,         "line 2 attributed to blender");
    std::printf("  blender verdict = 0x%04X, total = %lld paise\n",
                v.bits, (long long)v.cart_total_paise);
  }

  {  // accumulation: multiple reasons, never first-failure-only
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 9);   // qty 9 > cap 2
    auto v = evaluate(s, c.view(swiggy), NOW);
    CHECK(v.bits & R_QTY_EXCEEDED,        "qty cap enforced");
    CHECK(v.bits & R_CART_TOTAL_EXCEEDED, "and total cap, accumulated together");
  }

  {  // merchant allowlist
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 1);
    auto v = evaluate(s, c.view(t.intern("shady_merchant")), NOW);
    CHECK(v.bits & R_MERCHANT_NOT_ALLOWED, "merchant allowlist enforced");
  }

  {  // mandate TTL
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 1);
    auto v = evaluate(s, c.view(swiggy), 9'999'999'999ull);
    CHECK(v.bits & R_MANDATE_EXPIRED, "expired mandate denied");
  }

  {  // int64 overflow must be caught, never wrapped
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), INT64_MAX, 4);
    auto v = evaluate(s, c.view(swiggy), NOW);
    CHECK(v.bits & R_ARITH_OVERFLOW, "int64 overflow detected, not wrapped");
  }

  {  // determinism: same inputs -> byte-identical verdict (the audit property)
    Cart c; c.add(t.lookup("SKU_MEAL_THALI_001"), 24000, 1);
            c.add(t.intern("SKU_APPLIANCE_BLENDER_5"), 600000, 1);
    auto a = evaluate(s, c.view(swiggy), NOW);
    auto b = evaluate(s, c.view(swiggy), NOW);
    CHECK(verdict_equal(a, b), "evaluate() is deterministic");
  }

  {  // arena fails closed
    ScratchArena arena(256);
    CHECK(arena.alloc<std::uint32_t>(4) != nullptr, "small arena alloc succeeds");
    CHECK(arena.alloc<std::uint32_t>(100000) == nullptr, "arena exhaustion returns null");
    const auto g0 = arena.generation();
    arena.reset();
    CHECK(arena.generation() != g0, "reset bumps generation");
    CHECK(arena.used() == 0,        "reset rewinds head");
  }

  {  // intern round-trip
    InternTable t2;
    const auto id = t2.intern("SKU_MEAL_THALI_001");
    CHECK(id != InternTable::INVALID,            "intern returns a live id");
    CHECK(t2.intern("SKU_MEAL_THALI_001") == id, "intern is stable");
    CHECK(t2.name(id) == "SKU_MEAL_THALI_001",   "reverse lookup for audit");
    CHECK(t2.lookup("NEVER_SEEN") == InternTable::INVALID, "unknown sku -> INVALID");
  }

  std::printf("\n%d/%d checks passed%s\n", g_run - g_fail, g_run, g_fail ? "  <-- FAILURES" : "");
  return g_fail ? 1 : 0;
}
