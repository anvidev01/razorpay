> **Historical.** This was the plan written on day one. It is kept as a record of what
> was intended versus what was built; the shipped system is described in the README and
> docs 01, 02, 06-09.

# 05 — Execution Plan: 1 Sep (night) → 4 Sep, submit early

**Hard deadline 5 Sep. Target submission 4 Sep 21:00 IST** — a full day of slack,
because the single most common way to lose a hackathon is a rendering/upload failure
at 23:50.

**Governing principle:** the demo video is the deliverable. Code that does not appear
on camera or is not load-bearing for a judge's question is **cut**. Ruthlessly.

---

## Status: already done (tonight)

- ✅ Repo, structure, `.gitignore`, `scripts/log.sh`
- ✅ **All five blueprint documents**
- ✅ **Measured latency budget on the target machine** — kernel 28 ns, parse 160 ns,
  Ed25519 36.5 µs, `F_FULLFSYNC` 3 960 µs (`docs/BENCHMARKS.md`)
- ✅ **Working prototype kernel** (branch-free, benchmarked) — Day 2 does not start from zero
- ✅ **Reproduced 2 AM bug + regression test** (`engine/tests/repro_arena_uaf.cpp`)
- ✅ Parser decision settled by measurement: **simdjson**, not hand-rolled

> Two of the four hardest de-risking questions (is the kernel actually fast? is the
> incident story real?) are answered **before day one of the build**.

---

## Day 1 — Tue 2 Sep · The engine (highest risk, do it first)

| Block | Work | Done when |
|---|---|---|
| 09:00–11:00 | `ScratchArena`, `FixedPool`, generation handles, **ASan poisoning from the first commit** | unit tests green under ASan |
| 11:00–12:30 | SKU/merchant intern table; `IntentSchema` layout; `static_assert(sizeof/alignof)` | `sizeof(IntentSchema)` is 2 cache lines |
| 12:30–14:00 | **Kernel** — port the benchmarked prototype, all 10 verdict bits, overflow-checked `int64` paise | golden-vector tests pass |
| 14:00–15:30 | simdjson ingest → intern → SoA `CartView`; **oversize/malformed carts DENY** | fuzz 10 k malformed carts, zero crashes, zero allows |
| 15:30–17:00 | `rig-eval` CLI: `intent.json + cart.json → verdict` | blender fixture returns `0x000D` |
| 17:00–18:30 | Benchmark harness (batched timing, HdrHistogram) | reproduces 28 ns p50 |
| 18:30–19:30 | Ed25519 admission path (OpenSSL), SipHash integrity tag | tampered schema rejected |

**Gate:** `./rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json` prints three
reasons and a nanosecond timing. **If this is not working by 20:00, cut Ed25519 to a
stub and move on** — the kernel and the verdict are the demo, the crypto is a sentence.

## Day 2 — Wed 3 Sep · Audit trail + the "you can't bypass it" proof

| Block | Work | Done when |
|---|---|---|
| 09:00–11:30 | WAL writer: record format, BLAKE3 chain, CRC32C, segments | `wal-dump` renders records |
| 11:30–13:00 | **Group commit** (256 recs / 2 ms) on `F_FULLFSYNC` | measured ≈15 µs/decision amortised |
| 13:00–14:30 | PCT minter + Executor enforcing PCT, nonce burn, `cart_hash` recheck | 4 attack scripts all REFUSED |
| 14:30–16:00 | Java `WalReader` + `ChainVerifier` (`MappedByteBuffer`, **no preview flags**) | detects a tampered byte |
| 16:00–17:30 | Java `ReplayAuditor` → subprocess to C++ replay binary | 100 k replay, 0 divergent |
| 17:30–19:00 | `scripts/attack.sh` (4 bypass attempts), `scripts/tamper.sh` | both run clean on camera |
| 19:00–20:00 | Razorpay **test-mode** checkout wired behind the Executor | one real test payment succeeds |

**Gate:** all four bypass attempts refused, and the auditor detects tampering. **This is
the segment that wins the track** — it is the literal answer to "bounded and gated."

## Day 3 — Thu 4 Sep · Demo, video, submit

| Block | Work | Done when |
|---|---|---|
| 09:00–11:00 | Vanilla JS UI: chat pane, verdict panel, live WAL tail (SSE) | blender flashes red with 3 reasons |
| 11:00–12:00 | **Graceful failure path**: repair hints → auto-repair → lunch succeeds → step-up MFA → blender allowed | full loop on screen |
| 12:00–13:00 | Seed deterministic scenario (`--seed 42`); pre-generate histogram + auditor output | one command resets to demo state |
| 13:00–14:00 | README, architecture diagram, `docs/` final pass | a judge can run it from README alone |
| 14:00–16:00 | **Record.** Segments separately, to script. Retake the blender beat until clean | all segments in the can |
| 16:00–18:00 | Edit, subtitles, cut dead air, export 1080p60 | **≤ 5:00**, verified |
| 18:00–19:00 | Watch it end to end. Fresh-clone the repo and build from README | build works on a clean checkout |
| 19:00–21:00 | **Submit.** Buffer for upload failure | confirmation received |

---

## Cut line — drop in this order if behind

1. NEON vectorisation of the kernel *(already documented as a deliberate non-choice)*
2. Java FFM/Panama binding → keep the subprocess replay
3. Live Razorpay test-mode payment → mock the Executor, keep the PCT enforcement
4. Multi-segment WAL rotation → one segment file
5. The step-up/MFA re-approval beat → keep auto-repair *(cut this only under real duress; it is the "graceful" half of the bar)*

**Never cut:** the branch-free kernel, the verdict bitfield with multiple reasons, the
hash-chained WAL, the four bypass-attempt refusals, the replay auditor.
Those five *are* the submission.

## Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| BLAKE3 integration eats hours | med | fall back to SHA-256 via OpenSSL (already linked, measured) |
| `F_FULLFSYNC` makes the demo feel slow | med | group commit; show the 15 µs amortised number on screen |
| Razorpay test API friction | med | Executor mock behind the same interface; swap if time allows |
| Video runs > 5 min | **high** | script is timed to 4:55; record segments separately |
| Java/C++ integration rabbit hole | med | subprocess boundary, not FFI — decided already |
| Fresh-clone build fails for judges | med | Day 3 18:00 block is exactly this test |

## Per-commit discipline

Every block ends with:

```bash
./scripts/log.sh "day1-kernel" "branch-free kernel, 10 verdict bits, 28ns p50" --commit
```

This keeps `BUILD_LOG.md` a genuine, timestamped engineering record — which is itself
evidence for the "explainable" bar, and material for the write-up.
