# 03 — The 5-Minute Script · every word, in order

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

Read this on your second screen. **Say the words in the boxes exactly.** Everything else
is a stage direction.

```
SAY    speak these words
SPACE  press the spacebar — record.sh runs the command shown
WAIT   two seconds of silence. Do not fill it.
```

**Recording terminal:** `./scripts/record.sh --clean`
**Before every take:** `./scripts/seed.sh`

Numbers move a few ns by machine — **say what is on your screen**, not what is on this
page.

**Length: 894 spoken words + 6 pauses.** That is **5:18 at 175 wpm**, which is a normal
pace for rehearsed technical material. **If your first timed run is over five minutes,
apply the cut list at the bottom** — it removes 118 words and lands you at 4:36.

---

# 0:00 — The decision, measured

> **SPACE** → `./build/bench-engine-kernel 3`
> **SPACE** → `./build/bench-engine-kernel 8`
> **WAIT** — let both lines land.

**SAY**

> "That's an authorisation decision on a shopping cart. **Twenty-eight nanoseconds** for
> three lines, **fifty-eight** for eight — it scales, because it reads **every line**.
>
> The rail it protects costs **sixty-nine milliseconds**, so this check is **a
> millionth** of the request.
>
> **Checking is free** — which makes it worth asking why nothing in the payment chain
> actually does it."

---

# 0:28 — The gap

*No command. Keep the benchmark on screen and talk.*

**SAY**

> "**Agentic commerce** is shipping. NPCI's **UAP**, Razorpay's **Agent Pay**, Google's
> **AP2**, OpenAI's **ACP**. Every one asks two questions: is this a **registered
> agent**, and is the amount **under the limit**.
>
> **Neither reads the cart.**
>
> So a **₹62,000 tablet** in a ₹250 phone-case restock is under the limit, from a
> registered agent — and **every system approves it**. I built the layer that reads the
> cart."

---

# 0:55 — Inside the kernel

> **SPACE** → `sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40`

**SAY**

> "This is the **policy kernel**. **Branch-free** — no allocation, no syscall, no lock,
> **no floating point**. Money is **integer paise**, because a rounded fraction of a
> rupee becomes a reconciliation bug six months later.
>
> Violations accumulate into a **bitfield**, and it **never short-circuits** — you get
> **every** reason a cart failed, not the first. **Twenty-four reject codes**, with
> **per-line attribution**: 'line three failed, for these two reasons'. That costs
> **eight percent**, and `BENCHMARKS.md` states the price rather than hiding it.
>
> The verdict struct is deliberately **not** zero-initialised — zeroing 512 bytes measured
> as a **seven-nanosecond tax** per call.
>
> And it's a **pure function with no I/O**: same inputs, same verdict, any machine.
> **That matters more than the speed** — here's why."

---

# 1:35 — Durability

**SAY** *(before any command — this sets up the problem)*

> "A decision is worthless unless it's **durable before the money moves**. The payment
> token is minted **only after** the decision is on disk — so the disk is the bottleneck.
> And on macOS, the disk lies."

> **SPACE** → `grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp`

**SAY**

> "**`fsync` on macOS does not flush the drive's write cache** — it returns **success**
> while your bytes sit in volatile memory.
>
> The honest primitive is **`F_FULLFSYNC`**: **thirty-three microseconds for the
> comfortable lie, four milliseconds for the truth.** That caps you at **two hundred and
> fifty decisions a second** — so this was never a speed problem. It's an **amortised
> durability** problem."

> **SPACE** → `./build/rig-load 2000`
> **WAIT**

**SAY**

> "**Group commit**: batch 256 records or two milliseconds, then pay for **one** flush
> across all of them. Two hundred and forty-nine a second becomes **a hundred and
> fifty-three thousand**.
>
> And that log isn't only for durability. **It's the evidence.**"

---

# 2:20 — Two languages, one verdict

> **SPACE** → `./build/rig-audit wal/rig.wal`
> **SPACE** → `./build/rig-replay wal/rig.wal`

**SAY**

> "A **hash-chained write-ahead log** — every record carries the **SHA-256** of the one
> before it, single writer enforced by `flock`. And the engine **re-executes** every
> decision from its recorded inputs and gets the same verdict."

> **SPACE** → `java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal`
> **WAIT**

**SAY**

> "Now the same log through a **Java re-implementation that shares no code with the
> C++**. **Zero divergences.**
>
> One implementation agreeing with itself is a tautology. **Two independent
> implementations agreeing is evidence.**"

> **SPACE** → `./scripts/tamper.sh`

**SAY**

> "And flip one bit — the chain refuses to verify. **So the trail can't be edited. But
> can the gateway be bypassed?**"

---

# 2:55 — Try to break it

> **SPACE** → `./build/rig-attack`
> **WAIT · WAIT** — let the wall of REFUSED sit.

**SAY**

> "Eight ways to move money without permission. **All eight refused, exactly one
> legitimate payment authorised.**
>
> A **different device** — unenrolled key. **Raise the budget after signing** — the
> signature covers the exact bytes. **Skip the gateway** — there is no token at all.
>
> The mandate is signed **Ed25519 on the user's device**, and the gateway holds **only
> the public half** — so it **cannot forge a mandate even if you own the gateway**.
>
> Getting past this isn't prompt engineering. **It's forging Ed25519.**"

---

# 3:30 — Six industries, one number

> **SPACE** → `./scripts/sectors.sh`

**SAY**

> "And none of it is specific to one merchant. Same binary, different product feed —
> **SaaS seats** split across three lines to beat a twenty-five seat cap, and **₹1 today,
> ₹999 every month after**, denied while every limit-based check passes. **Nothing
> changed but the catalogue.**"

> **SPACE** → `./build/rig-revenue`

**SAY**

> "And this is the number a merchant cares about. **A declined agent cart is a lost sale,
> not a saved rupee.**
>
> **Ninety-eight percent of legitimate traffic completes with no human at all** —
> measured on a **held-out split**, on synthetic traffic I've **labelled as synthetic**."

---

# 4:03 — Graceful failure

> **SPACE** → `./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json`
> **WAIT**

**SAY**

> "The agent hallucinated an item. **Three violations at once**, the failing line named,
> and a **`repair` block** telling the agent what would make this pass. **No capability
> token** — this cart cannot reach the rail."

> **CUT TO THE BROWSER.** Scenario `3 · Auto-repair`. The only UI in the video.

**SAY**

> "The agent drops that line and resubmits. **The lunch still arrives.**
>
> Blocking is easy. **Blocking without breaking what the user wanted** is the product."

---

# 4:28 — The bug I found in myself

*Back to the terminal. Slow down. This is the beat they remember.*

**SAY**

> "I'll finish with what went wrong. I audited my own engine and found a **critical
> bypass**.
>
> **Quantity caps were enforced per line** — but the *agent* chooses how many lines it
> sends. So **ten lines of one item bought ten of a max-one item**, and it returned
> **`ALLOW`**. Every line was individually legal. The mandate said **at most one**.
>
> I found it writing an adversarial test, not from a failing case. Fixed with
> per-constraint aggregation — and `BENCHMARKS.md` records the before and after,
> **including the part I couldn't measure**, because the pre-fix revision no longer
> compiles.
>
> **That bug is the argument for this entire project.** Every layer above the kernel
> drifts, so the check has to sit at the **bottom**, read the **actual cart**, and be
> simple enough to **prove**.
>
> Clone it and run `./verify.sh`. **Forty-two checks — it exits non-zero if any of this
> was a lie.**"

---

## How the nine beats connect

Each beat ends on the sentence that opens the next. If you drop one, bridge it.

| beat | ends by asking | next beat answers |
|---|---|---|
| 0:00 measured | why does nobody check? | the rails don't read the cart |
| 0:28 the gap | what would reading it look like? | the kernel |
| 0:55 kernel | why does determinism matter? | it makes the log replayable |
| 1:38 durability | what is the log *for*? | evidence |
| 2:25 replay | can the trail be edited? can it be bypassed? | the attack wall |
| 3:05 attack | is this one merchant, or any? | six industries |
| 3:35 sectors | why would a merchant switch it on? | it doesn't break the sale |
| 4:05 graceful | what went wrong? | the bug |

## Running long? Cut exactly these

Timed yourself over 5:00? Delete these five passages, in this order. They total **118
words** and take you to **4:36**. Nothing above them depends on them.

**1 · the `tamper.sh` beat** (2:20) — 22 words. Skip the command too.

> ~~"And flip one bit — the chain refuses to verify. So the trail can't be edited. But can
> the gateway be bypassed?"~~
> Replace with: **"So — can the gateway be bypassed?"**

**2 · the paise aside** (0:55) — 16 words.

> ~~", because a rounded fraction of a rupee becomes a reconciliation bug six months
> later"~~ → end the sentence at **"integer paise"**.

**3 · one bypass example** (2:55) — 14 words.

> ~~"**Skip the gateway** — there is no token at all."~~

**4 · the SaaS example** (3:30) — 20 words. Keep the subscription one; it is the stronger
case because every limit-based check passes.

> ~~"**SaaS seats** split across three lines to beat a twenty-five seat cap, and"~~

**5 · the second half of the kernel close** (0:55) — 46 words.

> ~~"The verdict struct is deliberately not zero-initialised — zeroing 512 bytes measured
> as a seven-nanosecond tax per call."~~

Cut 5 **last**. It is the single detail that most convincingly shows you profiled rather
than guessed, so lose it only if the clock forces you.

**Never cut:** the closing beat, or any of the five lines below.

## The five lines that make it trustworthy

**Do not cut these for time.** Cut `tamper.sh` or the `rig-audit` line instead.

| line | what it buys |
|---|---|
| *"BENCHMARKS states the price rather than hiding it"* | you measure your own costs, including unflattering ones |
| *"including the part I couldn't measure"* | you mark the edge of what you verified |
| *"synthetic traffic I've labelled as synthetic"* | you don't dress a model up as a measurement |
| *"I found it writing an adversarial test"* | you attack your own work first |
| *"exits non-zero if any of this was a lie"* | you invite verification instead of asking for belief |

The bug is **last** on purpose. Opening with a mistake reads as apology; closing with one
reads as authority.

## Delivery

- **`WAIT` is two seconds of silence.** The viewer is reading the output. Talk over it
  and they process neither.
- **Never explain a command.** Only three are named — `kernel.cpp`, `clock.hpp` and the
  Java auditor — because there the file and the language *are* the claim.
- **Rehearse the hex**: `0x000D` blender · `0x2000` duplicate · `0x400000` subscription.
- Rehearse with `./scripts/record.sh` (cues visible), record with `--clean`.
- Retake one beat: `./scripts/record.sh --clean N`.

## Keyword coverage

agentic commerce · NPCI UAP · Agent Pay · AP2 · ACP · registered agent · scoped mandate ·
Ed25519 · asymmetric signing · public half · capability token · deterministic policy
kernel · branch-free · zero-allocation · integer paise · bitfield accumulation · never
short-circuits · 24 reject codes · per-line attribution · pure function · write-ahead log
· group commit · F_FULLFSYNC · amortised durability · hash-chained · SHA-256 · flock
single writer · tamper-evident · deterministic replay · zero divergences · independent
re-implementation · aggregate quantity · recurring commitment · graceful degradation ·
auto-repair · human-in-the-loop · held-out split · checkout completion · payments
infrastructure · bounded, gated, explainable
