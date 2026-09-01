# 01 — The Deterministic Policy Engine (C++)

## 0. The one invariant

> **A cart is payable if and only if a pure, allocation-free, branch-free function of
> `(signed_intent, cart)` returns `verdict == 0`.**

Everything else in this document exists to make that function *fast*, *replayable*,
and *impossible to route around*.

---

## 1. The measurement that dictates the architecture

| Operation | Measured p50 | Ratio vs kernel |
|---|---:|---:|
| Policy kernel, 8-line cart | **28 ns** | 1× |
| simdjson parse of the cart | 160 ns | 5.7× |
| **Ed25519 signature verify** | **36 458 ns** | **1 302×** |
| `F_FULLFSYNC` durability | 3 960 000 ns | 141 000× |

**Consequence:** signature verification *cannot* be on the per-cart hot path. A naive
design — "verify the signed mandate on every checkout" — is 36 µs, and the
"microsecond gateway" claim collapses. The architecture below is shaped by this.

### The resolution: verify once, admit, then tag

```
                 ADMISSION PATH (cold, ~37 µs, once per mandate)
  signed mandate ──> Ed25519 verify ──> canonicalise ──> IntentSchema in pool
                                                              │
                                                     128-bit integrity tag
                                                              │
                 HOT PATH (per cart, ~220 ns)                  ▼
  cart JSON ──> simdjson ──> intern ──> SoA ──> KERNEL(schema, cart) ──> verdict
                                                     ▲
                                          tag re-checked (~50 ns)
```

The mandate is verified **once**, at admission, and the verified `IntentSchema` is
frozen into a pool slot. The hot path re-checks a cheap keyed integrity tag
(SipHash-2-4 over the 128-byte record, ~50 ns) to prove the cached schema was not
mutated in memory. Cryptographic authenticity is established once; memory integrity is
established continuously.

---

## 2. Memory: three allocators, zero hot-path allocation

Borrowed directly from limit-order-book practice. Nothing on the hot path calls
`malloc`, `new`, or any syscall.

### 2.1 `ScratchArena` — per-request bump allocator

All per-request temporaries (the SoA cart arrays) live here.
Allocation is a pointer bump; deallocation is **one store**.

```cpp
class ScratchArena {
  std::byte*  base_;
  size_t      cap_;
  size_t      head_   = 0;
  uint32_t    gen_    = 0;          // generation, see §2.4
public:
  template <class T>
  T* alloc(size_t n) noexcept {
    size_t off = (head_ + 63) & ~size_t(63);       // 64B align every array
    if (off + n * sizeof(T) > cap_) return nullptr; // fail-closed, never grow
    head_ = off + n * sizeof(T);
    return reinterpret_cast<T*>(base_ + off);
  }
  void reset() noexcept {
    ASAN_POISON_MEMORY_REGION(base_, head_);        // see docs/04-INCIDENT-2AM.md
    head_ = 0;
    ++gen_;                                         // invalidates every outstanding handle
  }
};
```

Each array is 64 B-aligned so NEON loads are aligned and no cart line straddles a
cache line. `reset()` cannot fail and cannot fragment.

**`alloc` returning `nullptr` is not an error path to be papered over — it is a
`R_ENGINE_RESOURCE` verdict bit, i.e. a DENY.** The engine fails closed on its own
resource exhaustion. See §6.

### 2.2 `FixedPool<T, N>` — mandate storage

Live `IntentSchema` records occupy a pre-allocated contiguous slab with an
**index-based** intrusive free list.

```cpp
template <class T, uint32_t N>
class FixedPool {
  alignas(128) std::byte  storage_[N * sizeof(T)];
  uint32_t                free_head_;
  uint32_t                next_[N];      // free list as indices, not pointers
public:
  uint32_t acquire() noexcept;           // O(1), returns index or SENTINEL
  void     release(uint32_t) noexcept;   // O(1)
  T&       at(uint32_t i) noexcept { return reinterpret_cast<T*>(storage_)[i]; }
};
```

Indices, not pointers, because: 4 bytes instead of 8 (double the free-list density),
the whole pool stays relocatable and trivially snapshot-able for the audit log, and a
stale index is range-checkable whereas a stale pointer is not.

### 2.3 Per-thread arenas — no locks, no false sharing

Every worker owns its own `ScratchArena` and its own WAL ring. Nothing on the hot path
is shared, so nothing on the hot path needs an atomic. The only cross-thread structures
are the arena head and the WAL ring write index, and those are padded to **128 B**
(measured: 64 B captures 98 % of the win, 128 B a further 6 % — see BENCHMARKS §3).

### 2.4 Generation-tagged handles — the bug class, deleted

Any reference that outlives a request is a `{index, generation}` pair, never a raw
pointer:

```cpp
struct Handle { uint32_t idx; uint32_t gen; };
T* deref(Handle h) noexcept {
  return (h.gen == gen_) ? &at(h.idx) : nullptr;   // stale -> null, loudly
}
```

This converts a silent use-after-free into a testable null. It is the direct,
permanent fix for the incident in `docs/04-INCIDENT-2AM.md`, and the reason that bug
can never recur.

---

## 3. Data layout

### 3.1 `IntentSchema` — one mandate, cache-resident

```cpp
static constexpr int MAXC = 16;

struct alignas(128) IntentSchema {
  uint64_t mandate_id;
  uint64_t not_before_ns, not_after_ns;
  int64_t  total_budget_paise;          // integer paise. never a float.
  uint64_t merchant_allow_mask;         // bitset over 64 interned merchants
  uint32_t n_constraints;
  uint32_t schema_version;
  uint32_t sku_id       [MAXC];         // interned, sorted
  int64_t  max_unit_paise[MAXC];
  uint32_t max_qty      [MAXC];
  uint8_t  integrity_tag[16];           // SipHash-2-4, checked on hot path
};
```

Structure-of-arrays *inside* the struct (parallel `sku_id[] / max_unit_paise[] /
max_qty[]`) rather than an array of constraint structs: the kernel's inner loop
searches only `sku_id[]`, so keeping those 16 `uint32_t` contiguous puts the entire
search domain in **one 64-byte cache line**. An AoS layout would stride 16 bytes and
touch four lines for the same search.

Money is `int64_t` **paise**, everywhere, with `__builtin_mul_overflow` /
`__builtin_add_overflow` on every arithmetic step. There is no floating point anywhere
in the engine — a `double` cannot represent ₹0.01 exactly, and a rounding difference
between the engine and the payment rail is a reconciliation incident.

### 3.2 `CartView` — SoA, arena-backed

```cpp
struct CartView {
  const uint32_t* __restrict sku_id;      // 64B-aligned, from ScratchArena
  const int64_t*  __restrict unit_paise;
  const uint32_t* __restrict qty;
  uint32_t n;
  uint32_t merchant_local_id;
};
```

`__restrict` on all three lets the compiler keep the accumulator in a register across
the loop instead of reloading after every store.

### 3.3 SKU interning — strings die at the boundary

```cpp
struct alignas(64) Slot { uint64_t hash; uint32_t sku_id; uint32_t _pad; };  // 16B, 4/line
```

A SKU string is converted to a `uint32_t` **at the parse boundary and never again**.
Downstream — kernel, WAL, capability token — sees only integers. This is a
correctness property, not just a speed one: it is structurally impossible for a
`std::string_view` pointing into parser-owned memory to reach the WAL, which is
exactly the failure in `docs/04-INCIDENT-2AM.md`.

---

## 4. The kernel — branch-free, fixed-cost, total

```cpp
enum : uint32_t {
  R_OK                  = 0,
  R_SKU_NOT_IN_INTENT   = 1u << 0,
  R_QTY_EXCEEDED        = 1u << 1,
  R_UNIT_PRICE_EXCEEDED = 1u << 2,
  R_CART_TOTAL_EXCEEDED = 1u << 3,
  R_MERCHANT_NOT_ALLOWED= 1u << 4,
  R_MANDATE_EXPIRED     = 1u << 5,
  R_ARITH_OVERFLOW      = 1u << 6,
  R_REPLAY_NONCE        = 1u << 7,
  R_SCHEMA_VERSION      = 1u << 8,
  R_ENGINE_RESOURCE     = 1u << 9,   // arena exhausted -> DENY
};
```

Three properties, each deliberate:

**(a) It accumulates, it does not early-return.** Every rule is evaluated for every
cart and OR-ed into the verdict. A cart that violates three rules reports all three.
This is what makes the audit trail *complete* rather than *first-failure* — and it is
the direct technical answer to Razorpay's "every money action explainable".

**(b) It is branch-free on data.** Conditions become masks, not jumps:

```cpp
v |= (uint32_t)(-(int)(up > s.max_unit_paise[ci])) & R_UNIT_PRICE_EXCEEDED;
```

Measured consequence: p99/p50 = **1.29** with no tail on adversarial input. A
data-dependent branch would mispredict precisely on the attack case (the blender), so
the malicious path would be *slower* than the benign one — a timing side channel and an
availability risk. Here, benign and malicious carts cost the same.

**(c) It is a pure function.** `noexcept`, no allocation, no syscall, no lock, no
`clock_gettime`. The current time is sampled **once** at request entry and passed in as
a parameter. Therefore identical inputs produce a byte-identical verdict on any machine,
forever — which is what makes the offline replay auditor in `docs/02-AUDIT-WAL.md`
possible. **Determinism is not a performance feature here; it is the audit feature.**

```cpp
Verdict evaluate(const IntentSchema&, const CartView&, uint64_t now_ns) noexcept;
```

### On NEON / SIMD

The kernel is currently **scalar**, and that is the correct engineering call. At 28 ns
for 8 lines it is already ~5× cheaper than the parse that feeds it; vectorising the
constraint search would optimise 13 % of the decision cost and add an arm64-only code
path to audit. `MAXC = 16` also means the search domain is one cache line, so the
scalar loop is already memory-optimal. NEON is documented as a deliberate *non*-choice,
with the measurement that justifies it. (Note: this is arm64 — there is no AVX2 here.)

---

## 5. Latency budget

| Stage | Measured |
|---|---:|
| simdjson On-Demand parse + extract | 160 ns |
| intern 8 SKUs | ~30 ns |
| integrity tag re-check | ~50 ns |
| **kernel** | **28 ns** |
| **decision total** | **≈ 270 ns** |

---

## 6. Fail-closed, always

Money systems fail closed. Every one of these produces **DENY**, never allow:

- arena exhausted → `R_ENGINE_RESOURCE`
- malformed / oversized cart JSON → `R_SCHEMA_VERSION`
- unknown SKU (not in the intern table) → `R_SKU_NOT_IN_INTENT`
- integrity tag mismatch on the cached schema → hard abort, evict mandate, re-admit
- kernel exceeds its watchdog budget → DENY
- **engine unreachable / crashed → the executor denies** (§ `02-AUDIT-WAL.md`)

There is no configuration flag, anywhere, that turns a DENY into an ALLOW on error.
