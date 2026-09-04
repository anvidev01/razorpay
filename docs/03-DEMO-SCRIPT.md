# 03 — The 5-Minute Script · Mandate Engine

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

**Recording terminal:** `./scripts/record.sh --clean`
**Before every take:** `./scripts/seed.sh`
**Pace:** ~145 WPM. **WAIT** means two seconds of silence — let the output land.

**769 spoken words.** With pauses that is **5:38 at 145 WPM**, **5:17 at 155**, and
**4:47 at 165** — a normal pace for material you know cold. **Time one run.** If it goes
over, the cut list at the bottom removes 74 words and takes it to **4:56 at 145**.

Read what is on **your** screen. `rig-load` throughput swings between 117,000 and 153,000
run to run, and nanosecond figures shift with thermal state — say what you see, not what
is printed here.

---

# 0:00 — The problem, in one line

> *No command yet. Terminal on screen, empty prompt. Say this first.*

**SAY**

> "An AI agent can spend your money on something you never agreed to buy — and **every
> payment rail will approve it**. They check *who is asking* and *how much*. **Nothing
> checks the cart.**"

---

# 0:10 — The decision, measured

> **SPACE** → `./build/bench-engine-kernel 3`
> **SPACE** → `./build/bench-engine-kernel 8`
> **WAIT**

**SAY**

> "That's an authorization decision on a shopping cart. **Twenty-eight nanoseconds** for
> three lines, **fifty-eight** for eight. It scales, because it reads **every line**.
>
> The payment rail it protects costs about **seventy milliseconds**, so this check is a
> **millionth** of the request.
>
> **Checking is free** — which makes it worth asking why nothing in the payment chain
> actually does it."

---

# 0:38 — The gap

> *No command. Keep the benchmark on screen.*

**SAY**

> "**Agentic commerce** is shipping — NPCI's **UAP**, Razorpay's **Agent Pay**, Google's
> **AP2**, OpenAI's **ACP**. Every one asks: **registered agent**, and **under the
> limit**.
>
> So an agent told to buy a **₹400 lunch** can order a **₹6,000 blender** — under the
> **₹10,000 cap** — and **every system approves it**.
>
> I built the **Mandate Engine** to read the cart."

---

# 1:00 — Inside the kernel

> **SPACE** → `sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40`

**SAY**

> "This is the **deterministic policy kernel**. **Branch-free**, **zero-allocation**,
> strict **integer paise**. Violations use **bitfield accumulation** and never
> short-circuit — **24 reject codes** with exact **per-line attribution**.
>
> I measured what that costs: the verdict struct is deliberately **not** zero-initialised,
> because zeroing 512 bytes cost **seven nanoseconds** a call.
>
> And notice there is **no AI model here**. It's a **pure function** taking a mandate and
> a cart — which is what makes the audit trail **perfectly replayable**."

---

# 1:35 — Durability

> **SPACE** → `grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp`
> **SPACE** → `./build/rig-load 2000`
> **WAIT**

**SAY**

> "A decision is worthless unless it's **durable before the money moves**. But `fsync` on
> macOS **lies** — it returns success while your bytes sit in the **drive's volatile
> cache**.
>
> The honest command is **`F_FULLFSYNC`**: **thirty-three microseconds for the comfortable
> lie, four milliseconds for the truth.** Never a speed problem — an **amortised
> durability** problem.
>
> Using **group commit** I batch 256 records and pay for **one** flush — **250 decisions a
> second becomes over a hundred thousand**, and nobody waits more than two milliseconds."

---

# 2:20 — Two languages, one verdict

> **SPACE** → `./build/rig-audit wal/rig.wal`
> **SPACE** → `./build/rig-replay wal/rig.wal`

**SAY**

> "A **hash-chained write-ahead log**, **SHA-256**, **`flock` single writer**.
>
> Read the order: **mandate signed, cart proposed, decision, then token minted**. The
> token comes **after** the decision is durable — so there can never be a payment this log
> doesn't explain.
>
> And the engine runs a **deterministic replay** of its own log."

> **SPACE** → `java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal`
> **WAIT**

**SAY**

> "But I also built an **independent re-implementation in Java**. It reads the C++ log and
> gets **zero divergences**. Two independent systems agreeing is evidence — and that is
> what a **chargeback** turns on."

> **SPACE** → `./scripts/tamper.sh`

**SAY**

> "Flip **one bit** and the chain breaks at that record **and every record after it** —
> eight verified records become four. **Tamper-evident**: you can change it, you just
> cannot change it **quietly**."

---

# 2:55 — Try to break it

> **SPACE** → `./build/rig-attack`
> **WAIT · WAIT** — let the wall of REFUSED sit.

**SAY**

> "Can the gateway be bypassed? **Eight attacks — three that forge the human's
> authorisation, five that try to route around the engine entirely. All refused** — and
> replaying a used token fails because the **nonce is already burned**.
>
> The user signs a **scoped mandate** using **Ed25519 asymmetric signing** on their
> device. My gateway holds only the **public half**, so I **cannot forge a mandate even
> if I'm hacked**. Only valid carts get a **capability token**.
>
> Getting past this isn't prompt engineering. **It's forging Ed25519.**"

---

# 3:30 — Five industries, and the merchant's number

> **SPACE** → `./scripts/sectors.sh`
> **WAIT**

**SAY**

> "And none of this is specific to one merchant. **SaaS seats** split across three lines
> to beat a cap. The subscription dark pattern — **₹1 today, ₹999 a month after**.
>
> Five sectors, five controls, **one binary. Nothing changed but the catalogue.**"

> **SPACE** → `./build/rig-revenue`
> **WAIT**

**SAY**

> "And **agent-aware risk** is built in as defense-in-depth. Unusual behaviour doesn't
> auto-block — it escalates via **MFA**, so **zero legitimate sales are killed**. That
> **nets ₹50,000 ahead** of an aggressive blocker, and **98 percent completes with no
> human at all** — on a **held-out split** of traffic I've **labelled as synthetic**."

---

# 4:03 — Graceful failure

> **SPACE** → `./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json`
> **WAIT**

**SAY**

> "The agent hallucinated an item. The engine catches **three violations**, **names the
> failing line**, and sends an **auto-repair** hint. And it mints **no capability token**
> — so this cart cannot reach the rail at all."

> *Cut to the browser. Scenario `3 · Auto-repair`. The only UI in the video, ~20 seconds.*

**SAY**

> "All of this has a **merchant-facing interface**. The agent drops the blender,
> resubmits, and **the lunch still arrives** — **graceful degradation**. Blocking without
> breaking what the user wanted is the product."

---

# 4:28 — The bug I found in myself

> *Back to the terminal.*

**SAY**

> "I'll finish with what went wrong. I audited my engine and found a **critical bypass**.
>
> **Quantity limits were enforced per-line.** An agent could send **ten separate lines of
> a max-one item**, and the system **approved it**. I fixed it with **aggregate quantity**
> checks.
>
> I found it writing an **adversarial test**, not from a failing case — and `BENCHMARKS`
> records the before and after, **including the part I couldn't measure**, because the
> pre-fix revision no longer compiles.
>
> That bug is the argument for this project. **Every wrapper drifts. The check sits at
> the bottom.**"

> **SPACE** → `./verify.sh`
> **WAIT**

**SAY**

> "Clone it and run `verify.sh`. **Forty-two checks — it exits non-zero if any of this was
> a lie.** This is **bounded, gated, explainable payments infrastructure.**"

---

## If you have time spare

Time your run first. If you land under **4:40**, add the **retry storm** back — it is the
one control a judge is least likely to have seen, and it demonstrates rather than claims:

> **SPACE** → `./build/rig-eval fixtures/grocery_intent.json fixtures/cart_retry_a.json --wal wal/demo.wal`
> **SPACE** → `./build/rig-eval fixtures/grocery_intent.json fixtures/cart_retry_b.json --wal wal/demo.wal`
>
> **SAY:** "And when an agent times out and **re-generates** the request — a **retry
> storm** — **semantic idempotency** collapses it onto the original decision. **No second
> charge.**"

## If you run long

Delete these in order — **74 words**, taking it to **4:56 at 145 WPM**. Nothing else
depends on them.

- **the tamper beat** (2:20) — the command and its 32 words. Say *"and it's
  tamper-evident"* over the replay output instead.
- **the interface clause** (4:03) — *"All of this has a merchant-facing interface."*
  The browser is already on screen; it does not need announcing.
- **the sector examples** (3:30) — *"SaaS seats split across three lines to beat a cap.
  The subscription dark pattern — ₹1 today, ₹999 a month after."* The screen lists all
  five; keep only *"nothing changed but the catalogue."*

**Never cut:** the closing beat, the *"no AI model in this path"* line, the ordering
invariant at 2:20, or the two adversarial-test lines at 4:28.

## The five lines that make it trustworthy

| line | what it buys |
|---|---|
| *"I measured what that costs"* | you know the price of your own design choices |
| *"including the part I couldn't measure"* | you mark the edge of what you verified |
| *"synthetic traffic I've labelled as synthetic"* | you don't dress a model up as a measurement |
| *"I found it writing an adversarial test"* | you attack your own work before anyone else does |
| *"exits non-zero if any of this was a lie"* | you invite verification instead of asking for belief |

## Delivery

- **Never explain a command.** Only three are named — `kernel.cpp`, `clock.hpp`, and the
  Java auditor — because there the file and the language *are* the claim.
- **WAIT is two seconds of silence.** The viewer is reading output; talk over it and they
  process neither.
- **Rehearse the hex**: `0x000D` blender · `0x2000` duplicate · `0x400000` subscription.
- Rehearse with `./scripts/record.sh`, record with `--clean`, retake with `--clean N`.

## Keyword coverage

agentic commerce · NPCI UAP · Agent Pay · AP2 · ACP · registered agent · scoped mandate ·
Ed25519 · asymmetric signing · public half · capability token · deterministic policy
kernel · branch-free · zero-allocation · integer paise · bitfield accumulation · 24 reject
codes · per-line attribution · pure function · write-ahead log · group commit ·
F_FULLFSYNC · amortised durability · hash-chained · SHA-256 · flock single writer ·
tamper-evident · deterministic replay · zero divergences · independent re-implementation ·
chargeback · agent-aware risk · defense-in-depth · MFA escalation · aggregate quantity ·
semantic idempotency · retry storm · recurring commitment · auto-repair · graceful
degradation · held-out split · payments infrastructure · bounded, gated, explainable
