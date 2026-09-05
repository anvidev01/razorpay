# 03 — The 5-Minute Script · Mandate Engine

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

**Recording terminal:** `./scripts/record.sh --clean`
**Before every take:** `./scripts/seed.sh`, then fire one Razorpay call off camera — the
first call pays a TLS handshake (**3,315 ms cold vs 194 ms warm**).

**716 spoken words.** At 150 wpm that is **5:02** with pauses; **4:53** at 155.
Re-time against your own reading pace before committing to it.

Read what is on **your** screen. The 8-line kernel reads 50–59 ns depending on thermal
state, and `rig-load` moves a little run to run.

---

# 0:00 — Cold open

> **SPACE** → runs the blender denial. **Do not narrate the command.**
> **WAIT** — let `DENY 0x000D` and the three reasons land, then speak.

**SAY**

> "An AI agent just tried to buy a **six-thousand-rupee blender** out of a
> **five-hundred-rupee lunch budget**. Under the limit. Registered agent. It passes
> **every check a payment rail makes**.
>
> I didn't build a shopping agent. I built the layer underneath one, **that reads the
> cart**."

---

# 0:18 — Why now

> *No command. Keep the denial on screen.*

**SAY**

> "In February, **Razorpay and NPCI** put agentic payments on **Claude** — Zomato,
> Swiggy, Zepto. In four days NPCI is expected to unveil the **Unified Agent Protocol**
> at Global Fintech Fest. Google's **AP2** added a checkout mandate; **Amex** now
> underwrites purchases that *deviate from authenticated intent*.
>
> Everyone started building cart verification this year. **None of it is in UPI yet.**"

---

# 0:42 — The documented failure

> *Still no command.*

**SAY**

> "And it isn't hypothetical. OpenAI's **Operator** was asked to *find* the cheapest
> eggs. It **bought** them — thirty-one dollars on Instacart, no confirmation. OpenAI
> said it fell short of its own safeguards.
>
> The agent wasn't hacked. **It just did something nobody authorised.**"

---

# 1:00 — Inside the kernel

> **SPACE** → `sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40`

**SAY**

> "This is the **policy kernel**. **Branch-free**, **zero-allocation**, **integer
> paise**. Violations accumulate into a **bitfield** and it **never short-circuits** —
> every reason a cart failed, not the first. **Twenty-four reject codes**, **per-line
> attribution**.
>
> I measured what that costs: the verdict struct is deliberately **not**
> zero-initialised, because zeroing 512 bytes cost **seven nanoseconds** a call.
>
> And there is **no model in this path**. `evaluate` takes a mandate and a cart — **no
> utterance parameter**. A language model cannot reach this decision even in principle."

---

# 1:34 — Measured

> **SPACE** → `./build/bench-engine-kernel 3`
> **SPACE** → `./build/bench-engine-kernel 8`

**SAY**

> "**Twenty-eight nanoseconds** for three lines, **fifty-nine** for eight — it scales
> because it **reads every line**. The rail it protects takes about **two hundred
> milliseconds**. This check is **free**."

---

# 1:50 — Durability

> **SPACE** → `grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp`
> **SPACE** → `./build/rig-load 2000`
> **WAIT**

**SAY**

> "A decision is worthless unless it's **durable before money moves**. But `fsync` on
> macOS returns **success** while your bytes sit in the **drive's volatile cache**.
>
> The honest primitive is **`F_FULLFSYNC`** — **thirty-three microseconds for the lie,
> four milliseconds for the truth**. That caps you at **two hundred and fifty decisions
> a second**.
>
> **Group commit** amortises one flush across about **a hundred and twenty** decisions:
> **forty-seven microseconds** each, **twenty-one thousand a second**."

---

# 2:22 — Two languages, one verdict

> **SPACE** → `./build/rig-audit wal/rig.wal`
> **SPACE** → `./build/rig-replay wal/rig.wal`

**SAY**

> "Every decision lands in a **hash-chained log**. Read the order — **mandate signed,
> cart proposed, decision, then token minted**. The token comes **after** the decision
> is durable, so there can never be a payment this log can't explain.
>
> The C++ engine **replays its own log**."

> **SPACE** → `java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal`
> **WAIT**

**SAY**

> "Then a **Java re-implementation that shares no code** replays the same log and agrees.
> **Zero divergences.**"

> **SPACE** → `./scripts/tamper.sh`

**SAY**

> "Flip **one bit** and seven verified records become **three**."

---

# 2:56 — Try to break it

> **SPACE** → `./build/rig-attack`
> **WAIT · WAIT** — let the wall of REFUSED sit.

**SAY**

> "**Eight attacks** — three forging the human's signature, five routing around the
> engine. **All refused.**
>
> The mandate is signed **Ed25519 on the user's device**; the gateway holds **only the
> public half**, so it cannot forge a mandate **even if you own the gateway**.
>
> Getting past this isn't prompt engineering. **It's forging Ed25519.**"

---

# 3:19 — Five industries, and the merchant's number

> **SPACE** → `./scripts/sectors.sh`

**SAY**

> "Same binary, **five industries**. Thirty **SaaS seats** split across three lines to
> beat a twenty-five cap. **₹1 today, ₹999 a month after.** Nothing changed but the
> product feed."

> **SPACE** → `./build/rig-revenue`
> **WAIT**

**SAY**

> "Risk is built in as **defense-in-depth** — unusual behaviour **escalates to a human**
> instead of auto-blocking, so **zero legitimate sales are killed**. **A hundred percent
> of authorised value completes, ninety-eight percent of it unattended.** An aggressive
> blocker manages **ninety-four**."

---

# 3:49 — The moment the README can't give you

> **SPACE** → `./scripts/prove-razorpay.sh`
> **WAIT**

**SAY**

> "This part you can't get from reading the repo. That **order id was created seconds
> ago** — and this reads it **back out of Razorpay's API**, with the **mandate id** and
> the **WAL sequence number** stored against it. **Nothing local can fake that.**
>
> And the denied cart? **No order at all.** The rail was never contacted."

---

# 4:14 — The bug, and what to run

> *Speak first. Run the command underneath the last line.*

**SAY**

> "One last thing. I audited my own engine and found a **critical bypass**: quantity caps
> were enforced **per line**, but the *agent* chooses how many lines it sends. Ten lines
> of one item bought **ten of a max-one item**, and it returned **`ALLOW`**.
>
> Then I found **the same bug hiding in the Java auditor** I'd built to catch it. And a
> **benchmark quietly flattering itself by ten times**.
>
> All three fixed. All three pinned."

> **SPACE** → `./verify.sh --quick`
> **WAIT**

**SAY**

> "Run `verify.sh --quick` — **thirty-six checks in three seconds**. The full run is
> **forty-six**. **It exits non-zero if any of this was a lie.**"

---

## Why `--quick` on camera

The full `./verify.sh` takes **83 seconds** — 28% of a five-minute video spent watching a
progress list. `--quick` runs **36 checks in 3 seconds** and skips only the benchmarks and
the sanitizer rebuild. The line names the real full-suite number aloud, so nothing is
overstated. **Do not swap it back.**

## If you run long

Delete these in order. Nothing else depends on them.

- **the tamper line** (2:22) — the command and its 12 words
- **the documented failure** (0:42) — 44 words. Cut this only if you must; it is the
  strongest external evidence in the video.
- **the zero-init measurement** (1:00) — 24 words

**Never cut:** the cold open, the *"no model in this path"* line, the ordering invariant
at 2:22, the live Razorpay read at 3:49, or the closing beat.

## The five lines that make it trustworthy

| line | what it buys |
|---|---|
| *"I measured what that costs"* | you know the price of your own design choices |
| *"no utterance parameter"* | the AI-judgment claim is checkable in the signature, not asserted |
| *"nothing local can fake that"* | a third party confirms your claim on camera |
| *"the same bug hiding in the Java auditor"* | you found the second-order failure, not just the first |
| *"exits non-zero if any of this was a lie"* | you invite verification instead of asking for belief |

## Delivery

- **Never explain a command.** Only three are named — `kernel.cpp`, `clock.hpp`, and the
  Java auditor — because there the file and the language *are* the claim.
- **WAIT is two seconds of silence.** The viewer is reading output.
- **Rehearse the hex**: `0x000D` blender · `0x0002` split quantity · `0x400000` subscription.
- Rehearse with `./scripts/record.sh`, record with `--clean`, retake with `--clean N`.

## Keyword coverage

agentic commerce · NPCI · Unified Agent Protocol · Global Fintech Fest · AP2 · checkout
mandate · Amex authenticated intent · registered agent · scoped mandate · Ed25519 ·
asymmetric signing · public half · capability token · deterministic policy kernel ·
branch-free · zero-allocation · integer paise · bitfield accumulation · 24 reject codes ·
per-line attribution · no utterance parameter · write-ahead log · group commit ·
F_FULLFSYNC · amortised durability · hash-chained · SHA-256 · tamper-evident ·
deterministic replay · zero divergences · independent re-implementation · aggregate
quantity · defense-in-depth · MFA escalation · held-out split · Razorpay Orders API ·
payments infrastructure · bounded, gated, explainable
