# 03 — The 5-Minute Recording Script

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

Terminal-first. The UI appears once, for twenty seconds, at 4:05. Every number is
re-checked by `./verify.sh` (42/42) — if a claim here stops being true, verification
fails before the video does.

## Setup

```
./scripts/seed.sh      # fresh WAL, mandate windows refreshed
./verify.sh            # must read: 42 passed, 0 failed
```

Terminal at ~18pt, 1080p60. Warm the binaries once — the first run pays page-fault cost.
Then drive the whole thing with **`./scripts/record.sh`**: one beat per keypress, nothing
to copy or mistype on camera.

| | segment | ends |
|---|---|---|
| 0:00 | Cold open — the decision, measured | 0:30 |
| 0:30 | The gap in agentic payments | 0:55 |
| 0:55 | Inside the kernel — why C++ | 1:35 |
| 1:35 | Durability — why `fsync` lies | 2:25 |
| 2:25 | Two languages, one verdict | 3:05 |
| 3:05 | Try to break it | 3:35 |
| 3:35 | One engine, six industries | 4:05 |
| 4:05 | Graceful failure — the only UI | 4:30 |
| 4:30 | The bug I found in myself | 5:00 |

---

## 0:00 – 0:30 · Cold open

**No title card. No face. Start on a prompt.**

```
./build/bench-engine-kernel 3
./build/bench-engine-kernel 8
```

```
cart_lines= 3  production kernel: p50=  28.2 ns  p99=  31.7 ns  min= 25.2 ns
cart_lines= 8  production kernel: p50=  58.0 ns  p99=  63.8 ns  min= 52.4 ns
```

> "That's an authorisation decision on a shopping cart. **Twenty-eight nanoseconds.**
>
> It scales with the cart, because it reads **every line**. The payment rail it protects
> costs **sixty-nine milliseconds** — so the safety layer is about **a millionth** of the
> request.
>
> That's the thesis. **Checking is free. So there is no excuse for not checking.**"

---

## 0:30 – 0:55 · The gap

> "**Agentic commerce** is shipping. NPCI's **UAP**, Razorpay's **Agent Pay**, Google's
> **AP2**, OpenAI's **ACP**. Every one answers two questions: is this a **registered
> agent**, and is it under the **transaction limit**.
>
> Neither reads the cart.
>
> So a ₹62,000 tablet in a phone-accessories restock is under the limit, from a
> registered agent — and **every system in the chain approves it.**
>
> I built the layer that reads the cart."

---

## 0:55 – 1:35 · Inside the kernel (your C++ segment)

```
sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40
```

> "This is the whole security argument, so let me be specific about the engineering.
>
> It is a **deterministic, branch-free policy kernel**. **No allocation, no syscall, no
> lock, no floating point** — money is **integer paise** end to end, because a float
> that rounds a fraction of a rupee is a bug you find in a reconciliation report six
> months later.
>
> Violations accumulate as a **bitfield**. The kernel **never short-circuits** — you get
> **every** reason a cart failed, not the first one. Twenty-four distinct reject codes.
>
> And it does **per-line attribution**: not 'this cart was denied' but 'line three was
> denied, for these two reasons'. That costs about eight percent of the kernel, and
> `docs/BENCHMARKS.md` states the price rather than hiding it.
>
> One deliberate line — the verdict struct is **not** zero-initialised, because
> zeroing 512 bytes of line results measured as a **7-nanosecond tax** on every call.
> Only the lines actually used are ever written or read."

*Then the payoff:*

> "Because it is a pure function with no I/O, the same inputs give the same verdict on
> any machine, forever. **That property is what makes the audit trail replayable** — and
> replay is what turns a log into evidence."

---

## 1:35 – 2:25 · Durability

> "A decision is worthless unless it's **durable before the money moves**. So the payment
> token is minted **only after** the decision is on disk. Which makes disk the
> bottleneck — and on macOS, disk lies."

```
grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp
```

> "**`fsync()` on macOS does not flush the drive's write cache.** It returns **success**
> while your bytes sit in volatile cache. Lose power, lose the write — and `fsync`
> already told you it was safe.
>
> The honest primitive is **`fcntl(F_FULLFSYNC)`**. And it is brutally expensive:
>
> **thirty-three microseconds for the comfortable lie. Four milliseconds for the truth.**
> A hundred and eighteen times more.
>
> Four milliseconds a decision caps you at **two hundred and fifty a second**. That is not
> a payment gateway. So this was never a matching-speed problem — it is an
> **amortised-durability** problem."

```
./build/rig-load 2000
```

```
commit every record       249/s     61 fsyncs, 238.77 ms in F_FULLFSYNC
group commit (256/2ms) 153,121/s     1 fsync,    3.17 ms
```

> "**Group commit.** Batch 256 records or two milliseconds, whichever comes first, and pay
> for **one** `F_FULLFSYNC` across all of them.
>
> Two hundred and forty-nine a second becomes **a hundred and fifty-three thousand**.
> Sixty-one flushes became **one**. Same honest fence — and nobody waits longer than two
> milliseconds, because the timer closes the batch when traffic is light.
>
> **Full durability, six microseconds a decision.**"

---

## 2:25 – 3:05 · Two languages, one verdict

```
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json \
  --wal wal/rig.wal
./build/rig-audit wal/rig.wal
```

> "A **hash-chained write-ahead log**. Every record carries the **SHA-256** of the one
> before it and a **CRC32C** over its own body. One writer, enforced by `flock` — two
> processes appending to one chain corrupt it irrecoverably, so it fails loudly instead."

```
./build/rig-replay wal/rig.wal
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal
```

```
replay    : 2 decisions re-executed by a SEPARATE implementation
divergent : 0

OK  C++ engine and Java auditor agree on every money action
```

> "The C++ engine replays its own log from recorded inputs. Then a **Java
> re-implementation that shares no code with it** replays the same log and agrees.
> **Zero divergences.**
>
> One implementation agreeing with itself is a tautology. **Two independent
> implementations agreeing is evidence.** For my C++ to hide a policy bug, the Java
> would have to have the identical bug."

```
./scripts/tamper.sh
```

> "Flip **one bit** anywhere and the chain refuses to verify and names the record. A
> database row you can edit quietly. **This you cannot.**"

---

## 3:05 – 3:35 · Try to break it

```
./build/rig-attack
```

*Let the wall of REFUSED sit for two full seconds before speaking.*

> "Eight ways to move money without permission. **All eight refused. Exactly one
> legitimate payment authorised.**
>
> Sign with a **different device** — refused, unenrolled key. **Raise the budget after
> signing** — refused, the signature covers the exact bytes. **Skip the gateway** — no
> token exists, and the executor accepts nothing else. **Replay a used token** — nonce
> already burned.
>
> The mandate is signed **Ed25519 on the user's device**. The gateway holds **only the
> public half — it cannot forge a mandate even if you own the gateway.** That is the
> difference from a shared secret, where the verifier can always mint its own.
>
> The agent holds **no Razorpay credentials at any point.** Getting around this isn't
> prompt engineering. **It's forging Ed25519.**"

*If you have four seconds spare, this is the strongest ad-lib in the video:*

> "Paste malformed JSON at it live if you like — I ran twenty-seven fuzz cases through
> **ASan and UBSan**. Zero findings. Every one returns a clean `DENY`."

---

## 3:35 – 4:05 · One engine, six industries

```
./scripts/try.sh --sector saas "buy 10 standard seats under 12000 rupees" \
  SKU_SEAT_STANDARD:900:10
./scripts/sectors.sh
```

> "Same binary. **Retail** — a ₹62,000 tablet in an accessories restock. **SaaS** — thirty
> seats split across three lines against a twenty-five cap, caught on **aggregate
> quantity**. **Travel** — the second flight breaks the budget. **Procurement** — an
> unapproved vendor. **Subscriptions** — ₹1 today, ₹999 every month after, denied while
> **every limit-based check passes**.
>
> **Nothing changed but the product feed.** There is not one food concept in the kernel.
> This is **payments infrastructure**, not a shopping demo."

```
./build/rig-revenue
```

```
Agent checkout completion
  intent gateway      100.0% of authorised value completed (98.3% unattended)
  aggressive blocker   94.0% of authorised value completed
```

> "And the number a merchant actually cares about. **A declined agent cart is a lost
> sale, not a saved rupee.** An uncertain signal becomes **one tap**, not a refusal — so
> **ninety-eight percent completes with no human at all**, and nothing the human
> authorised is permanently lost."

---

## 4:05 – 4:30 · Graceful failure — the only UI

```
./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json \
  --wal wal/demo.wal
```

```
DENY  verdict = 0x000D
  |- R_SKU_NOT_IN_INTENT · R_UNIT_PRICE_EXCEEDED · R_CART_TOTAL_EXCEEDED

  ok    SKU_MEAL_THALI_001
  ok    SKU_DRINK_LIME_007
  DENY  SKU_APPLIANCE_BLENDER_5

repair {"remove":["SKU_APPLIANCE_BLENDER_5"],"resubmit_ok":true,
        "escalate":{"path":"new_mandate","requires":"user_mfa"}}
```

> "The agent hallucinated an item. **Three violations, per-line attribution, and a
> `repair` block that says what would make this pass.** No token minted — this cart
> **cannot** reach the rail."

*Now, and only now, cut to the browser. Twenty seconds. Scenario `3 · Auto-repair`.*

> "The agent drops the line and resubmits. **The lunch still arrives.**
>
> Blocking is easy. **Blocking without breaking what the user actually wanted** is the
> product — and that is why a merchant would switch this on."

---

## 4:30 – 5:00 · The bug I found in myself

*Back to the terminal. No slides. Just say it.*

> "I audited my own engine and found a **critical bypass**.
>
> **Quantity caps were enforced per line.** But the *agent* chooses how many lines it
> sends. So ten lines of one item bought ten of a max-one item — and it returned
> **`ALLOW`**. Verdict `0x0000`. Clean. No violation, because every line was individually
> legal.
>
> The mandate said **at most one**.
>
> I found it writing an adversarial test, not from a failing case. Fixed with
> per-constraint aggregation, and pinned so it cannot come back.
>
> **That bug is the argument for this entire project.** Every layer above the kernel
> drifts — models change, prompts change, agents get updated. So the check has to sit at
> the **bottom**, read the **actual cart**, and be simple enough to **prove**.
>
> Clone it. Run `./verify.sh`. **Forty-two checks, and it exits non-zero if any of this
> was a lie.**"

---

## Recording notes

- **Rehearse the hex.** `0x000D` blender · `0x0800` substitution · `0x2000` duplicate ·
  `0x1000` injection · `0x400000` subscription. From memory, it reads as fluency.
- **Silence sells.** Two seconds on the `rig-attack` wall and on `divergent: 0` beats two
  more sentences.
- **Read your screen, not this page.** Numbers move a few ns by machine and thermal
  state — that is the point of shipping the benchmark.
- `./scripts/seed.sh` before every take. `./scripts/record.sh N` to retake one beat.

## If you only get 90 seconds

`bench-engine-kernel` → `rig-load` (249/s → 153k/s) → Java `divergent: 0` → blender
denied with `repair`. **Fast, durable, reproducible, graceful.**

## Keyword coverage

agentic commerce · agentic payments on UPI · NPCI UAP · Agent Pay · AP2 · ACP ·
autonomous agent · scoped mandate · Ed25519 · asymmetric signing · public half ·
capability token · nonce burn · deterministic policy kernel · branch-free ·
zero-allocation · integer paise · bitfield accumulation · never short-circuits ·
per-line attribution · 24 reject codes · pure function · write-ahead log · group commit ·
F_FULLFSYNC · amortised durability · hash-chained · CRC32C · flock single writer ·
tamper-evident · deterministic replay · zero divergences · independent
re-implementation · ASan · UBSan · aggregate quantity · semantic idempotency · prompt
injection · recurring commitment · graceful degradation · auto-repair ·
human-in-the-loop · held-out test set · checkout completion rate · dispute evidence ·
Razorpay Orders API · payments infrastructure · bounded, gated, explainable
