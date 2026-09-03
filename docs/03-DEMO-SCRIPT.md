# 03 — The 5-Minute Recording Script (terminal-first)

**Track 01 · AI Growth & Agentic Commerce**

Systems-engineering cut. The UI appears for ~20 seconds and never leads. Every number
below was measured on **Apple M4, Release, 2026-09-03** and is re-checked by
`./verify.sh` (40/40).

## Setup

```bash
./scripts/seed.sh          # fresh WAL, mandate windows refreshed
./verify.sh                # must read: 40 passed, 0 failed
```

Two terminals, ~18pt, 1080p60. **Terminal A** runs commands. **Terminal B** stays on
`watch -n1 './build/rig-audit wal/rig.wal | tail -20'` so the log grows on camera.

Warm the binaries once before recording — first run pays page-fault cost.

| | segment | ends |
|---|---|---|
| 0:00 | Cold open — the decision, measured | 0:35 |
| 0:35 | The gap it closes | 0:55 |
| 0:55 | Durability: why `fsync` lies on macOS | 1:55 |
| 1:55 | Dual-audit across two languages | 2:45 |
| 2:45 | Graceful failure and auto-repair | 3:35 |
| 3:35 | Try to get around it | 4:05 |
| 4:05 | One engine, five industries | 4:25 |
| 4:25 | The bug I found in myself | 5:00 |

---

## 0:00 – 0:35 · Cold open

**No title card. No face. Start on a prompt.**

```bash
./build/bench-engine-kernel 3
./build/bench-engine-kernel 8
```

```
cart_lines= 3  production kernel: p50=   28.2 ns  p99=   31.7 ns  min=  25.2 ns
cart_lines= 8  production kernel: p50=   58.0 ns  p99=   63.8 ns  min=  52.4 ns
```

> "That's an authorisation decision on a shopping cart. **Twenty-eight nanoseconds** for
> a three-line cart, **fifty-eight** for eight lines — it scales with the cart, because
> it actually reads every line.
>
> A **deterministic, branch-free policy kernel**. No allocation, no syscall, no lock, no
> floating point — money is **integer paise** end to end. Same inputs, same verdict,
> on any machine, forever.
>
> The payment rail it protects costs **sixty-nine milliseconds**. So the safety layer is
> roughly **a millionth** of the request. That's the thesis: **correctness here is free,
> so there is no excuse for not checking.**"

---

## 0:35 – 0:55 · The gap

> "**Agentic commerce** is shipping — NPCI's agentic payments on UPI, Razorpay's **Agent
> Pay**, Google's **AP2**, OpenAI's **ACP**. They all answer two questions: is this a
> **registered agent**, and is it under the **transaction limit**.
>
> Neither one reads the cart.
>
> So a six-thousand-rupee blender instead of a four-hundred-rupee lunch is under the
> ten-thousand limit, from a registered agent — and **every system in the chain approves
> it.** I built the layer that reads the cart."

---

## 0:55 – 1:55 · Durability (the 60 seconds that matter)

> "Here's the part that decides whether this is real infrastructure or a demo.
>
> A decision is worthless unless it's **durable before the money moves**. So I mint the
> payment token **only after the decision is on disk**. Which makes disk the bottleneck —
> and on macOS, disk lies to you."

```bash
grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp
```

> "**`fsync()` on macOS does not flush the drive's write cache.** It pushes your bytes to
> the device and returns success — while they sit in volatile cache. Lose power and
> they're gone. `fsync` returned zero and you still lost the write.
>
> Apple's answer is **`fcntl(fd, F_FULLFSYNC)`**, which tells the drive to actually flush.
> It's honest, and it is **brutally** expensive."

*Point at the headline table in `docs/BENCHMARKS.md`:*

```
WAL fsync                          33.5 µs    <- does not flush drive cache
WAL fcntl(F_FULLFSYNC)          3 960 µs      <- true durability. 118x more.
```

> "**Thirty-three microseconds for the comfortable lie. Four milliseconds for the truth.**
> A hundred and eighteen times more expensive.
>
> Four milliseconds per decision caps you at **two hundred and fifty decisions a second**.
> That's not a payment gateway. So the whole design problem isn't matching speed — it's
> **amortised durability**."

```bash
./build/rig-load 2000
```

```
commit every record          60 decisions in 240.9 ms ->  4015.2 us/decision (    249/s)
                             61 fsyncs, 238.77 ms in F_FULLFSYNC,  1.0 decisions/fsync

group commit (256/2ms)     2000 decisions in  13.1 ms ->     6.5 us/decision ( 153121/s)
                              1 fsyncs,   3.17 ms in F_FULLFSYNC, 2000.0 decisions/fsync
```

> "**Group commit.** Batch up to 256 records or two milliseconds, whichever comes first,
> then pay for **one** `F_FULLFSYNC` across all of them.
>
> Two hundred and forty-nine decisions a second becomes **a hundred and fifty-three
> thousand**. Same honest fence — sixty-one flushes became **one**.
>
> And nobody waits more than two milliseconds, because the timer closes the batch even
> when traffic is light. **Full durability, six microseconds a decision.** That is the
> entire design."

---

## 1:55 – 2:45 · Dual-audit

```bash
F=fixtures                      # keeps every line short enough to copy safely

./build/rig-eval $F/lunch_intent.json $F/lunch_cart.json \
  --wal wal/rig.wal
./build/rig-eval $F/lunch_intent.json $F/blender_cart.json \
  --wal wal/rig.wal
./build/rig-audit wal/rig.wal
```

> "Two decisions in a **hash-chained write-ahead log**. Every record carries the SHA-256
> of the one before it, plus a CRC32C over its own body. Chain **intact**."

```bash
./build/rig-replay wal/rig.wal
```

```
chain     : 14 records, SHA-256 chain INTACT
replay    : 2 decisions re-executed against recorded inputs
divergent : 0
```

> "The engine **re-executes** every decision from the recorded inputs and gets the same
> verdict. That's what determinism buys you — the log doesn't just say what happened, it
> **proves it follows from the inputs**."

```bash
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal
```

```
replay    : 2 decisions re-executed by a SEPARATE implementation
divergent : 0

OK  C++ engine and Java auditor agree on every money action
```

> "Now the same log through a **Java re-implementation that shares no code with the C++**.
> Different language, different author sitting, same policy. **Zero divergences.**
>
> One implementation agreeing with itself is a tautology. **Two independent
> implementations agreeing is evidence.** If my C++ has a policy bug, the Java has to have
> the identical bug to hide it."

```bash
./scripts/tamper.sh
```

> "And flip **one bit** anywhere in that log — the chain refuses to verify and names the
> record. A database row you can edit quietly. This you cannot."

---

## 2:45 – 3:35 · Graceful failure

> "Blocking is easy. Blocking **without breaking the user's goal** is the product."

```bash
./build/rig-eval $F/lunch_intent.json $F/blender_cart.json \
  --wal wal/demo.wal
```

```
DENY  verdict = 0x000D   eval = 584 ns

reasons (all of them -- the kernel never short-circuits)
  |- R_SKU_NOT_IN_INTENT      item is not in the approved intent
  |- R_UNIT_PRICE_EXCEEDED    unit price exceeds the per-item cap
  |- R_CART_TOTAL_EXCEEDED    cart total exceeds the mandate budget

per-line attribution
  ok    SKU_MEAL_THALI_001
  ok    SKU_DRINK_LIME_007
  DENY  SKU_APPLIANCE_BLENDER_5    R_SKU_NOT_IN_INTENT R_UNIT_PRICE_EXCEEDED

repair {"remove":["SKU_APPLIANCE_BLENDER_5"],"resubmit_ok":true,
        "escalate":{"path":"new_mandate","requires":"user_mfa"}}

no capability token minted -- this cart cannot reach the payment rail
```

> "The agent hallucinated a blender. **Three violations at once** — the kernel never stops
> at the first, so you get the whole story, not the first complaint.
>
> **Per-line attribution**: the thali is fine, the drink is fine, *line three* is the
> problem. That's what the 8% explainability cost buys.
>
> Then the last line — **`repair`**. The engine doesn't just refuse, it says **what would
> make this pass**: drop this SKU and resubmit. Or escalate to a **new mandate with MFA**
> if the human genuinely wants a blender.
>
> And no **capability token** was minted, so this cart physically cannot reach the rail.
> Refusal isn't a flag someone can ignore downstream — **there is nothing to pay with.**"

*Cut to the UI for ~20 seconds. Scenario `3 · Auto-repair`.*

> "The agent drops the line and resubmits. **ALLOW. The lunch still arrives.** The user
> asked for lunch and got lunch. The guardrail did not become an outage — and *that* is
> why merchants would switch this on."

---

## 3:35 – 4:05 · Try to get around it

```bash
./build/rig-attack
```

*Let the wall of REFUSED sit for two full seconds before speaking.*

> "Eight ways to move money without permission. **All eight refused. Exactly one
> legitimate payment authorised.**
>
> Sign with a **different device** — refused, unenrolled key. **Raise the budget after
> signing** — refused, the signature covers the exact bytes. **Skip the gateway** — no
> token, and the executor accepts nothing else. **Replay a used token** — nonce already
> burned.
>
> The mandate is signed **Ed25519** on the user's device. The gateway holds **only the
> public half — it cannot forge a mandate even if you own the gateway.** That's the
> difference from a shared secret, where the verifier can always mint its own.
>
> And the agent holds **no Razorpay credentials at any point**. Getting around this isn't
> prompt engineering. It's forging Ed25519."

---

## 4:05 – 4:25 · One engine, five industries

```bash
./scripts/sectors.sh
```

> "Same binary. **Retail** — a sixty-two thousand rupee tablet in an accessories restock.
> **SaaS** — thirty seats split across three lines against a twenty-five cap; caught on
> **aggregate quantity**. **Travel** — second flight breaks the budget. **Procurement** —
> unapproved vendor. **Subscriptions** — one rupee today, nine ninety-nine a month after,
> denied `0x400000` while every limit-based check passes.
>
> **Nothing changed but the fixtures.** There is not one food concept in the kernel. This
> is **payments infrastructure**, not a shopping demo."

---

## 4:25 – 5:00 · The bug I found in myself

> "I audited my own engine and found a **critical bypass**.
>
> **Quantity caps were enforced per line.** And the *agent* decides how many lines it
> sends. So ten lines of one item bought ten of a max-one item — and it returned
> **`ALLOW`**, with verdict `0x0000`. Clean. No violation. The mandate said *at most one*.
>
> Fixed with per-constraint aggregation, and pinned in the tests so it cannot come back.
> Closing it made the kernel measurably slower, and `docs/BENCHMARKS.md` records the
> before and after — including the part I **couldn't** measure, because the pre-fix
> revision no longer compiles.
>
> **That bug is the argument for the whole project.** Every layer above the kernel drifts
> — models change, prompts change, agents get updated. So the check has to sit at the
> bottom, read the actual cart, and be simple enough to prove.
>
> Clone it. Run `./verify.sh`. **Forty checks, and it exits non-zero if any of this was a
> lie.**"

---

## Command reference — the whole project from a terminal

```bash
# Setup
./scripts/seed.sh                 # fresh WAL + refreshed mandate windows
./verify.sh                       # 40 checks; non-zero exit if any claim is false
./verify.sh --quick               # ~15s, skips benchmarks and sanitizers

# Performance
./build/bench-engine-kernel 3     # 28.2 ns  (demo cart)
./build/bench-engine-kernel 8     # 58.0 ns
./build/rig-load 2000             # group commit: 249/s -> 153,121/s

# One decision, fully explained
./build/rig-eval $F/lunch_intent.json $F/blender_cart.json \
  --wal wal/rig.wal
./build/rig-eval ... --json       # same thing, machine-readable

# The audit trail
./build/rig-audit  wal/rig.wal    # every money action, in order, with reasons
./build/rig-replay wal/rig.wal    # C++ re-executes its own log
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal
./scripts/tamper.sh               # flip one bit -> chain refuses to verify

# Adversarial
./build/rig-attack                # 8 bypasses, all refused
./scripts/sectors.sh              # 5 industries, one binary

# Evidence & metrics
./build/rig-evidence wal/rig.wal <decision_id>   # dispute pack
./build/rig-riskeval              # held-out precision/recall
./build/rig-revenue               # four-policy revenue comparison
./scripts/prove-razorpay.sh       # create an order, read it back off Razorpay's API

# The UI, if you want it
./run.sh                          # http://127.0.0.1:8787
```

## Recording notes

- **Rehearse the hex codes.** `0x000D` blender · `0x0800` substitution · `0x2000`
  duplicate · `0x1000` injection · `0x400000` subscription.
- **Let output land before speaking.** Two seconds of silence on the `rig-attack` wall
  and on the Java `divergent: 0` is worth more than two extra sentences.
- `./scripts/seed.sh` before every take.
- Numbers drift by machine and thermal state. **Read what's on your screen**, not what's
  printed here — that's the whole point of shipping the benchmark.

## If you only get 90 seconds

`bench-engine-kernel` → `rig-load` (249/s → 153k/s) → Java auditor `divergent: 0` →
blender denied with repair. That's the thesis: **fast, durable, reproducible, graceful.**

## Keyword coverage

agentic commerce · agentic payments on UPI · NPCI · Agent Pay · AP2 · ACP · autonomous
agent · scoped mandate · Ed25519 · asymmetric signing · capability token · deterministic
policy kernel · branch-free · integer paise · per-line attribution · write-ahead log ·
group commit · F_FULLFSYNC · amortised durability · hash-chained · tamper-evident ·
CRC32C · deterministic replay · zero divergences · independent re-implementation ·
graceful degradation · auto-repair · human-in-the-loop · prompt injection · recurring
commitment · aggregate quantity · held-out test set · dispute evidence · Razorpay Orders
API · payments infrastructure
