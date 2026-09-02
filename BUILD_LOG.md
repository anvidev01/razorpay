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

### 2026-09-01 18:34:33Z — phase0-briefing
- **at:** `2026-09-01 18:34:33Z` (HEAD `d19fa26`)
- added standalone visual briefing page (log-scale latency ladder, verdict decode, bypass matrix)

### 2026-09-02 — Day 1: the engine, the WAL, and the audit loop

Built the whole spine. Six bugs found by building rather than by planning — each one
changed the design.

**1. `R_OK` collided with a POSIX macro.** `<unistd.h>` defines `R_OK` as
"test for read permission". The verdict enum used the same name, so any translation
unit including both failed to compile — which the WAL and the HTTP server both do.
Renamed to `R_NONE` with a comment so it never comes back. *Caught at first integration,
not at first design.*

**2. The WAL hash chain was broken by field ordering.** `this_hash` covers the record
header, and the CRC lives *inside* that header. I computed the hash first and wrote the
CRC second, so the bytes on disk differed from the bytes that were hashed and every
verification failed. Fixed by ordering: CRC, then chain. A one-line reorder, but it
would have invalidated the entire audit claim.

**3. The WAL had no recovery path.** Each process started with `head_ = 0`, so a second
process appending to an existing log wrote `prev_hash = 0` and broke the chain at the
boundary. Implemented real recovery: scan on open, resume from the verified head, and
**truncate a torn tail** (a partial write from a crash). The verified prefix is the
surviving log — which is exactly what a WAL is supposed to do on restart.

**4. Two writers silently destroyed the log.** I accidentally ran a second gateway
against the same WAL and corrupted it beyond repair — both processes held their own
in-memory chain head, so each invalidated the other's `prev_hash`. Added
`flock(LOCK_EX|LOCK_NB)` on open: a second writer now fails immediately with a clear
error instead of quietly shredding the audit trail. **This is the failure mode that
would have been catastrophic in production and invisible in a demo.**

**5. `Verdict v{}` cost 7ns per call.** Zero-initialising `lines[MAX_CART]` (512 bytes)
on every evaluation. Only the first `n_lines` entries are ever written or read, so the
memset was pure waste. Removed it and added `verdict_equal()`, because a raw `memcmp`
over `Verdict` is now invalid — the tail is deliberately unwritten.

**6. The group-commit number in the docs was wrong.** `02-AUDIT-WAL.md` claimed
15.5 µs/decision, calculated as 3960 µs / 256. Wrong twice over: each decision writes
**three** records (so a 256-record batch holds ~85 decisions), and the 2 ms timer
usually closes the batch first. Measured with `rig-load`: **37 µs/decision at
~27 000/s**, 125 decisions per fsync. Replaced the estimate with the measurement and
documented why the estimate was wrong. Also found that un-grouped mode fences *twice*
per decision (0.5 decisions/fsync), which is why it costs 7.5 ms rather than 4 ms.

**Measurements taken.** Interleaved A/B, best-of-5, to control for machine drift
(the same binary measured 28.3 / 36.5 / 40.8 ns across runs depending on load — single
runs are worthless):
- prototype kernel (verdict bits only): **28.4 ns** p50, 8-line cart
- production kernel (+ per-line attribution): **30.8 ns** p50
- **per-line explainability costs +2.4 ns (+8%)** — the price of the audit trail, stated

**The audit claim got stronger than planned.** The Java control plane began as a WAL
reader. It became an *independent reimplementation of the policy kernel*: its own record
parser, own CRC32C, own SHA-256 chaining, own `evaluate()`. Re-running the C++ engine
over its own log only proves self-consistency. Two implementations in different
languages agreeing on every decision proves the **policy is well-specified** — that the
audit trail means something regardless of which binary produced it. Both report
0 divergent.

Also shipped: simdjson ingest with strings interned at the boundary (the permanent fix
for the 2 AM bug class), Ed25519 admission with a SipHash integrity tag on the hot path,
capability tokens with nonce burn and cart-hash binding, five refused bypass attempts,
tamper demo, HTTP server, and the vanilla-JS demo UI.

**Crypto verified against published vectors, not just "it runs":** SipHash-2-4-128 of
the empty input under the reference key = `a3817f04ba25a8e66df67214c7550293`;
CRC32C("123456789") = `e3069283`; SHA-256("abc") = `ba7816bf…f20015ad`.
