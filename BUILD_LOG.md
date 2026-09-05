# Build Log — Mandate Engine

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

---

## Phase 1 — the engine (02 Sep, 05:59–06:17)

Nine commits: zero-allocation core, deterministic kernel, hash-chained WAL with
recovery and an exclusive-writer lock, simdjson ingest, capability tokens, gateway
orchestration with group commit, the auditor and bypass suite, the loopback UI, and an
**independent Java implementation of the same policy**.

The Java auditor exists so "explainable" is checkable rather than asserted: it shares no
code with the C++ engine and must agree bit-for-bit on every recorded decision.

**Estimates replaced with measurements** the same day — every number in the docs comes
from a benchmark that ships in the repo.

## Phase 2 — the four agentic blind spots (12:30)

Researched first, built second. All four confirmed real and current, with sources in
`docs/06`. NPCI's Reserve Pay cap is exactly ₹10,000/90 days; Razorpay + NPCI announced
agentic payments on Claude in Feb 2026.

Built: substitution policy (the ₹180-organic-for-₹60-toned case), injection telemetry
(explicitly *not* the security boundary), semantic idempotency that survives restart,
and the dispute evidence pack.

## Phase 3 — three outcomes, and Track 02 (13:26–14:10)

ALLOW / REVIEW / DENY instead of a binary, so a probabilistic signal escalates to the
human rather than killing a legitimate sale. Human step-up with a confirmation record
bound to the cart hash. Payment execution through the rail.

**Bug found by driving the real UI:** `execute()` passed merchant id 0 to `observe()`,
so the behavioural baseline never learned any merchant and `R_NEW_MERCHANT` fired
forever. The Track 02 detector was useless outside its own harness.

## Phase 4 — making it usable (15:11–16:21)

Compose-your-own-order, a one-command runner, and natural language → draft mandate
(the model proposes, the human signs — the model holds no key).

**The translator broke three times, each caught by using it:**
1. "mojito" silently became a lime soda — an unrequested substitution, the exact failure
   this project exists to prevent, in my own code.
2. My fix used a hardcoded food-word list, so "chicken tikka" became chicken biryani.
   Replaced with a rule derived from the merchant's own catalogue vocabulary.
3. That fix over-corrected: an unknown dish two words away killed a *valid* neighbour
   ("suji halwa with mojito" dropped the mojito). Adjacency tightened to immediate.

25 regression tests pin all three.

## Phase 5 — security audit (16:39)

Tested the running system rather than reading it. Six findings, detail in `docs/09`.

**Critical:** quantity caps were enforced *per line*, and the agent chooses how many
lines it sends — ten lines of qty 1 bought ten of a max-one item, `ALLOW 0x0000`. The
core claim of the project, silently broken.

**High:** the mandate was signed *and* verified with the gateway's own key, so it only
proved the gateway agreed with itself. A separate `UserDevice` now holds the signing
key; the gateway is enrolled with the public half and cannot sign. Three forgeries are
demonstrated refused. Also `Access-Control-Allow-Origin: *` on a money endpoint, and
evidence packs emitting malformed JSON above 4 KB.

## Phase 6 — production readiness (18:12–19:50)

`./verify.sh` — one command, 27 checks, non-zero exit on failure.

**The project was macOS-only.** `clock_gettime_nsec_np` and `F_FULLFSYNC` are Darwin
extensions and CMake hardcoded Homebrew paths, so a judge following the Debian
instructions in `run.sh` could not build it at all. Portable clock layer, cross-platform
CMake, CI on Ubuntu *and* macOS.

Real Razorpay test-mode integration verified end to end: an order created, then **read
back out of `api.razorpay.com`** carrying `mandate_id`, `decision_id` and `wal_seq`.
Traceability is bidirectional — from a disputed charge to the signed intent, and back.

Connection reuse took the rail from 3.4 s cold / 163–200 ms steady to 176 ms cold /
**69–95 ms warm**.

## Phase 7 — the Track 02 bar, taken literally (20:13)

The bar says *"precision and recall on a held-out test set."* Mine were 24 hand-written
cases I had **also tuned the thresholds against**, reporting precision 1.00 / FPR 0.00.

On a proper held-out split that same detector scored **precision 0.278, FPR 12.1%**.
The earlier numbers were fiction.

Root cause was design, not tuning: firing on *any* single signal made `R_NEW_MERCHANT`
an accusation, and 906 legitimate transactions in the set were at a merchant the agent
had not used before. Signals are now weighted and must corroborate.

```
held-out: precision 0.908  recall 0.301  FPR 0.002  (0.20% of good traffic prompted)
train F1 0.401 vs test 0.452 — no meaningful overfitting
```

Recall 0.30 is reported as the honest number, with the full tradeoff curve.

**Two harness bugs found alongside it**, both of which would have produced misleading
numbers: a failed threshold sweep silently reported untuned defaults as chosen, and a
`printf` format mismatch read a pointer as a double and segfaulted. The build had no
warning flags. It does now — `-Wall -Wextra -Wformat=2`, zero warnings — which also
surfaced dead code behind the "3 meals of different category" complaint.

---

## What this log is for

Every entry above that begins with a bug is one I introduced and then found by
**using the thing**, not by reading it. The `max_qty` bypass, the fictional metrics and
the silent substitutions were all in code I had already called finished.

That is the argument for the architecture, not an embarrassment to it: the layer above
the kernel drifts, so the kernel has to check the cart — and the numbers have to come
from data the tuning never saw.

