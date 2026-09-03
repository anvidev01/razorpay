# 03 — The 5-Minute Script (spoken)

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

Say the words in the boxes. Nothing else. Every line is folded — it names the file, flag
or language **and** makes the claim in one breath.

**770 spoken words + 11 marked pauses.** At a technical-demo pace of **165 wpm that is
5:02**; at 175 it is **4:46**. You will speak faster than you expect on material you
know. **`‖` means stop talking for two seconds** — the pauses are scripted, not slack.

Drive with `./scripts/record.sh` — one beat per keypress, nothing to type or paste.
Numbers shift a few ns by machine: **read your screen, not this page.**

| | beat | words | ends |
|---|---|---|---|
| 0:00 | The decision, measured | 53 | 0:28 |
| 0:28 | The gap | 58 | 0:52 |
| 0:52 | Inside the kernel | 122 | 1:35 |
| 1:35 | Durability | 102 | 2:22 |
| 2:22 | Two languages, one verdict | 70 | 2:55 |
| 2:55 | Try to break it | 73 | 3:30 |
| 3:30 | Six industries, one number | 84 | 4:03 |
| 4:03 | Graceful failure | 58 | 4:28 |
| 4:28 | The bug I found in myself | 151 | 5:00 |

---

## 0:00 · The decision, measured

`./build/bench-engine-kernel 3` then `8`

> "That's an authorisation decision on a shopping cart. **Twenty-eight nanoseconds** for
> three lines, **fifty-eight** for eight — it scales, because it reads **every line**. ‖
>
> The rail it protects costs **sixty-nine milliseconds**: the safety layer is **a
> millionth** of the request. **Checking is free, so there's no excuse for not
> checking.**"

---

## 0:28 · The gap

*No command. Talk over the previous screen.*

> "**Agentic commerce** is shipping — NPCI's **UAP**, Razorpay's **Agent Pay**, Google's
> **AP2**, OpenAI's **ACP**. Every one asks: is this a **registered agent**, and is it
> under the **limit**.
>
> Neither reads the cart. So a **₹62,000 tablet** in a phone-case restock passes every
> check.
>
> I built the layer that reads the cart."

---

## 0:52 · Inside the kernel

`sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40`

> "Here's the kernel — **branch-free**, no allocation, no syscall, no lock, **no floating
> point**. Money is **integer paise**, because a rounded fraction of a rupee is a
> reconciliation bug six months later.
>
> Violations accumulate as a **bitfield**, and it **never short-circuits** — you get
> **every** reason a cart failed, not the first.
>
> **Per-line attribution** — 'line three, for these two reasons', not just 'denied'. It
> costs eight percent, and `BENCHMARKS.md` states the price rather than hiding it. ‖
>
> The verdict struct is deliberately **not** zero-initialised: zeroing 512 bytes measured
> as a **seven-nanosecond tax** per call.
>
> And it's a **pure function with no I/O** — same inputs, same verdict, any machine.
> **That is what makes the audit trail replayable.**"

---

## 1:35 · Durability

`grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp`

> "Here's the portability layer — **`fsync` on macOS does not flush the drive's write
> cache.** It returns **success** while your bytes sit in volatile cache. ‖
>
> The honest primitive is **`F_FULLFSYNC`**: **thirty-three microseconds for the
> comfortable lie, four milliseconds for the truth.**
>
> That caps you at **two hundred and fifty decisions a second** — so this was never a
> speed problem. It's an **amortised durability** problem."

`./build/rig-load 2000`

> "**Group commit**: batch 256 records or two milliseconds, pay for **one** flush across
> all of them. ‖
>
> Two hundred and forty-nine a second becomes **a hundred and fifty-three thousand**.
> Sixty-one flushes became **one**."

---

## 2:22 · Two languages, one verdict

`./build/rig-audit wal/rig.wal` then `./build/rig-replay wal/rig.wal`

> "A **hash-chained write-ahead log** — every record carries the **SHA-256** of the one
> before it. The engine **re-executes** every decision from its recorded inputs and gets
> the same verdict."

`java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal`

> "Now the same log through **a Java re-implementation that shares no code with the
> C++**. **Zero divergences.** ‖
>
> One agreeing with itself is a tautology. **Two independent implementations agreeing is
> evidence.** And flip one bit — the chain refuses to verify."

`./scripts/tamper.sh`

---

## 2:55 · Try to break it

`./build/rig-attack`

> "Eight ways to move money without permission. ‖ ‖
>
> **All eight refused. One legitimate payment authorised.**
>
> A **different device** — unenrolled key. **Raise the budget after signing** — the
> signature covers the exact bytes.
>
> The mandate is signed **Ed25519 on the user's device**, and the gateway holds **only
> the public half** — it cannot forge a mandate **even if you own the gateway**.
>
> Getting past this isn't prompt engineering. **It's forging Ed25519.**"

*Only if you're ahead of time — a strong offer, but cut it before you overrun:*

> "Paste malformed JSON at it if you like — twenty-seven fuzz cases through **ASan and
> UBSan**, zero findings."

---

## 3:30 · Six industries, one number

`./scripts/sectors.sh`

> "Same binary, different product feed — **SaaS seats** split across lines to beat a cap,
> and **₹1 today, ₹999 a month after**, denied while every limit check passes.
>
> **Nothing changed but the catalogue.** There isn't one food concept in the kernel."

`./build/rig-revenue`

> "And the number a merchant cares about. **A declined agent cart is a lost sale, not a
> saved rupee.** ‖
>
> **Ninety-eight percent completes with no human at all** — measured on a **held-out
> split**, on synthetic traffic I've **labelled as synthetic**."

---

## 4:03 · Graceful failure

`./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json`

> "The agent hallucinated an item. **Three violations at once**, the failing line named,
> and a **`repair` block** saying what would make this pass. No token — **this cart
> cannot reach the rail.** ‖"

*Cut to the browser. Twenty seconds. Scenario `3 · Auto-repair`. The only UI in the video.*

> "The agent drops the line and resubmits. **The lunch still arrives.**
>
> Blocking is easy. **Blocking without breaking what the user wanted** is the product."

---

## 4:28 · The bug I found in myself

*Back to the terminal. Slow down. This is the beat they remember.*

> "I audited my own engine and found a **critical bypass**.
>
> **Quantity caps were enforced per line.** But the *agent* chooses how many lines it
> sends — so ten lines of one item bought **ten of a max-one item**, and it returned
> **`ALLOW`**. No violation, because every line was individually legal. ‖
>
> The mandate said **at most one**.
>
> I found it writing an adversarial test, not from a failing case. Fixed with
> per-constraint aggregation — and `BENCHMARKS.md` records the before and after,
> **including the part I couldn't measure**, because the pre-fix revision no longer
> compiles.
>
> **That bug is the argument for this project.** Every layer above the kernel drifts. So
> the check sits at the **bottom**, reads the **actual cart**, and is simple enough to
> **prove**.
>
> Clone it. Run `./verify.sh`. **Forty-two checks, and it exits non-zero if any of this
> was a lie.**"

---

## Why this reads as trustworthy

Five lines do that work. **Do not cut them to save time** — cut the optional ASan line
instead.

| line | what it buys |
|---|---|
| *"BENCHMARKS states the price rather than hiding it"* | you measure your own costs, including unflattering ones |
| *"including the part I couldn't measure"* | you mark the edge of what you verified — the rarest signal in a demo |
| *"synthetic traffic I've labelled as synthetic"* | you don't dress a model up as a measurement |
| *"I found it writing an adversarial test"* | you attack your own work before anyone else does |
| *"exits non-zero if any of this was a lie"* | you invite verification instead of asking for belief |

The bug story is **last** on purpose. Opening with a mistake reads as apology; closing
with one reads as authority — you spent four minutes proving competence, so the admission
lands as confidence.

## Delivery

- **Never narrate a command.** Only three get named — `kernel.cpp`, `clock.hpp`, and the
  Java auditor — because the file and the language *are* the claim.
- **`‖` is two seconds of silence.** The viewer is reading the output; talk over it and
  they process neither.
- **Rehearse the hex**: `0x000D` blender · `0x2000` duplicate · `0x400000` subscription.
- `./scripts/seed.sh` before every take. `./scripts/record.sh N` to retake one beat.
- **If you overrun**, cut in this order: the ASan offer, then `tamper.sh`, then the
  `rig-audit` line. Never the last beat.

## If you only get 90 seconds

Kernel benchmark → `rig-load` (249/s → 153k/s) → Java `divergent: 0` → the max_qty bug.
**Fast, durable, independently reproducible, and honest about its own failure.**

## Keyword coverage

agentic commerce · NPCI UAP · Agent Pay · AP2 · ACP · registered agent · scoped mandate ·
Ed25519 · asymmetric signing · public half · capability token · deterministic policy
kernel · branch-free · zero-allocation · integer paise · bitfield accumulation · never
short-circuits · per-line attribution · pure function · write-ahead log · group commit ·
F_FULLFSYNC · amortised durability · hash-chained · SHA-256 · tamper-evident ·
deterministic replay · zero divergences · independent re-implementation · ASan · UBSan ·
aggregate quantity · recurring commitment · graceful degradation · auto-repair ·
human-in-the-loop · held-out split · checkout completion · payments infrastructure ·
bounded, gated, explainable
