# Build Log — Razorpay Intent Gateway

A timestamped engineering record. Appended via `./scripts/log.sh "<phase>" "<note>" --commit`.

### 2026-09-01 — Phase 0: blueprint + de-risking
- **Environment audit.** Target is Apple M4 / arm64, macOS 15.7.7, Apple clang 17,
  JDK 21, Node 24. Consequences recorded before writing any engine code:
  - **no AVX2** (NEON only), **no `rdtscp`**, **no `perf`**, **no Valgrind on arm64 macOS**, **no gdb**.
  - Toolkit is therefore ASan/UBSan/TSan + LLDB + Instruments.
- **Timer resolution measured first.** mach timebase = 125/3 → **41.667 ns/tick**;
  `mach_absolute_time()` costs 8.9 ns. A 28 ns kernel is *smaller than one timer tick*,
  so all kernel benchmarks use batched timing (1 000 iters/window × 20 000 windows).
  Establishing this before benchmarking avoided publishing meaningless single-shot numbers.
- **Prototype kernel built and measured:** branch-free, SoA, `int64` paise.
  8-line cart **p50 28.3 ns / p99 36.4 ns**, linear at ~3.5 ns per line, p99/p50 = 1.29.
- **Parser decision made by measurement, reversing my initial instinct.**
  Hand-rolled fixed-schema scanner 149 ns p50 / **282 ns p99**; simdjson 4.6.9 On-Demand
  160 ns p50 / **248 ns p99**. simdjson is equal at p50, *better* at p99, and hardened
  against adversarial input. **Chose simdjson.** The hand-rolled scanner bought nothing.
- **Ed25519 measured: 36 458 ns p50 — 1 302× the kernel.** This single number reshaped the
  architecture: signature verification moved off the hot path to a one-time admission
  step, with a cheap keyed integrity tag re-checked per cart.
- **`F_FULLFSYNC` measured: 3 960 µs p50** vs `fsync()` 33.5 µs. On macOS `fsync()` does
  **not** flush the drive write cache, so an `fsync`-based WAL is not durable and the
  audit guarantee would have been fiction. Group commit (256 recs/2 ms → ~15.5 µs
  amortised) is now a core design element rather than an optimisation.
- **False-sharing granule measured:** 64 B separation gives 5.9×; 128 B a further 6 %.
  Padding everything to 128 B would halve L1 density for little gain — 128 B reserved for
  the two hottest cross-thread fields only.
- **2 AM bug reproduced and committed as a test** (`engine/tests/repro_arena_uaf.cpp`).
  Confirmed empirically that ASan is **completely silent** on a use-after-free inside a
  pool allocator (exit 0), and catches it in 9 s once the arena poisons itself on reset.
- Blueprint documents written: `docs/01`…`docs/05` + `docs/BENCHMARKS.md`.

**Net effect:** the two highest-risk unknowns — *is the kernel actually fast?* and *is the
incident story real?* — were both answered before the build sprint began.

### 2026-09-01 18:30:37Z — phase0-fix
- **at:** `2026-09-01 18:30:37Z` (HEAD `52c625a`)
- corrected build-log year to 2026; regenerated mandate TTL fixtures against real clock
