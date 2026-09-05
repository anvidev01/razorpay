# 03 — The 5-Minute Script · Mandate Engine

**Razorpay AI Buildathon · Track 01 — AI Growth & Agentic Commerce**

**Recording terminal:** `./scripts/record.sh --clean`
**Before every take:** `./scripts/seed.sh`, then fire one Razorpay call off camera — the
first call pays a TLS handshake (**3,315 ms cold vs 194 ms warm**).

**748 spoken words.** At 150 wpm that is **5:19** with pauses; **5:09** at 155 and **5:00** at 160. This is as lean as it gets without dropping something a judge scores — **time one run**, and if you are still over, cut the `tamper`/audit narration at 2:22.
Re-time against your own reading pace before committing to it.

Read what is on **your** screen. The 8-line kernel reads 50–59 ns depending on thermal
state, and `rig-load` moves a little run to run.

---

# −0:25 — The hook  *(the only face-to-camera moment in the video)*

> **FACE TO CAMERA.** Nothing on screen but you. This is the **only** time you appear —
> everything after this is screen recording.
> Look **directly into the lens**, not at your own preview. Neutral expression, a slight
> smile at most. **No gestures** — hands still, this is too short to need them.
> Terminal already open behind the camera, `record.sh --clean` waiting on beat 1.

**SAY**

> "In April, American Express promised to cover purchases that **deviate from what a
> customer authorised** — then shipped five services and left the one that **checks the
> cart** unbuilt.
>
> Other demos show an agent buying something. **This is the layer that decides whether
> it's allowed to.**
>
> I attacked it myself this week. **It failed three times** — all three are in this video."

> **CUT TO SCREEN SHARE** on the word **"video"**. Do not pause between the two — the
> terminal should already be filling the frame as the word ends. Then press **SPACE** and
> continue from the cold open below.

> **VOICE** — Level and factual, like reading a release note; the Amex line is doing the
> work, don't oversell it. Small stress on **checks the cart** — that clause is the whole
> pitch. Real beat before *"Other demos"*. **Do not smile on "failed three times."** Say
> it the way you would report a test result, then cut.

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

> **VOICE** — Energised but level, not salesy. Hit **six-thousand** and **five-hundred**
> hard; the gap between them is the whole hook. Full stop after *"rail makes"* — two
> beats. Drop half a tone on the last line and slow it: that sentence is the positioning.

---

# 0:18 — Why now

> *No command. Keep the denial on screen.*

**SAY**

> "In February, **Razorpay and NPCI** put agentic payments on **Claude** — Zomato,
> Swiggy, Zepto. In four days NPCI is expected to unveil the **Unified Agent Protocol**
> at Global Fintech Fest. Google's **AP2** added a checkout mandate.
>
> Everyone started building cart verification this year. **None of it is in UPI yet.**"

> **VOICE** — Brisk through the four names; this is setup, not the point. Land on **four
> days** — it is the only urgency you have. Pause before the last sentence, then say it flat.

---

# 1:00 — Inside the kernel

> **SPACE** → `sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40`

**SAY**

> "This is the **policy kernel**. **Branch-free**, **zero-allocation**, **integer
> paise**. Violations accumulate into a **bitfield** and it **never short-circuits** —
> every reason a cart failed, not the first. **Twenty-four reject codes**, **per-line
> attribution**.
>
> And there is **no model in this path**. `evaluate` takes a mandate and a cart — **no
> utterance parameter**. A language model cannot reach this decision even in principle.
>
> **Twenty-eight nanoseconds**, on a checkout that takes two hundred milliseconds. **The
> check is free.**"

> **VOICE** — Fastest beat in the script; these are credentials, deliver them cleanly and
> keep moving. Slow only for **seven nanoseconds**. Then stop, and take the last paragraph
> deliberately — *no utterance parameter* is the line a judge will quote.

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
> four milliseconds for the truth** — which caps you at **two hundred and fifty a
> second**.
>
> **Group commit** amortises one flush across about **a hundred and twenty** decisions:
> **forty-seven microseconds** each, **twenty-one thousand a second**."

> **VOICE** — Relish **success** — that word is the whole betrayal. Slow right down through
> *thirty-three microseconds / four milliseconds*; it is the sharpest contrast you have.
> Speed back up for group commit and let the last two numbers land as a pair.

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

> **VOICE** — Read the ordering list as four separate items, not one phrase. Two full beats
> on **zero divergences** — longest pause in the video. The tamper line is a throwaway: fast, dry.

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

> **VOICE** — Say nothing for two seconds while REFUSED fills the screen. Then flat and
> unhurried — the wall is doing the work. Final line: slow, quiet, no lift at the end.

---

# 3:19 — Five industries, and the merchant's number

> **SPACE** → `./scripts/sectors.sh`

**SAY**

> "Same binary, **five industries**. Thirty **SaaS seats** split across three lines to
> beat a twenty-five cap.
>
> And **₹1 today, ₹999 every month after** — under budget, right merchant, real SKU.
> **Every limit-based check passes it.** Denied, because the mandate authorised a
> purchase, not a **subscription**.
>
> A timed-out agent that **re-generates** its request — different bytes, same basket —
> collapses onto **one charge**. Nothing changed but the product feed."

> **SPACE** → `./build/rig-revenue`
> **WAIT**

**SAY**

> "**Agent-aware risk** escalates to a human instead of auto-blocking, so **zero
> legitimate sales are killed**. **A hundred percent of authorised value completes,
> ninety-eight percent unattended.** An aggressive blocker manages **ninety-four**."

> **VOICE** — Quick through the sectors, they are illustration. Slow for **zero legitimate
> sales are killed**. Then **hundred / ninety-eight / ninety-four** as three separate beats.

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

> **VOICE** — Lean in. This is the one thing the repo cannot show. Point at the order id as
> you say **seconds ago**. Beat before **nothing local can fake that**, then say it slowly.

---

# 4:05 — The third outcome  *(the only UI beat)*

> **CUT TO BROWSER** → `http://127.0.0.1:8787`, already open on **Scripted scenarios**
> **CLICK** → `5 · Hidden instructions`
> **WAIT** — `REVIEW 0x1000`, `R_INJECTION_SUSPECTED`, and the step-up card appears

**SAY**

> "Allow and deny you've seen. This is the third outcome — every item inside the mandate,
> only the text is hostile. So it **asks**, instead of killing a real sale."

> **CLICK** → `Approve · MFA`
> **WAIT** — *"human approved at step-up — token minted, payment executed"*

**SAY**

> "One tap. **Token minted, paid.**"

> **VOICE** — Fast and light; this is a coda, not a new act. Slow only on **asks**. Say the
> last four words on the click, then cut straight back to the terminal without a breath —
> the next line starts immediately.

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

> **VOICE** — Drop the energy. Matter-of-fact, almost bored — you are reporting, not
> confessing. Slight pause before **`ALLOW`**. *"All three fixed. All three pinned."* —
> clipped, four beats, no apology in the voice.

> **SPACE** → `./verify.sh --quick`
> **WAIT**

**SAY**

> "Run `verify.sh --quick` — **thirty-six checks in three seconds**. The full run is
> **forty-six**. **It exits non-zero if any of this was a lie.**"

> **VOICE** — Direct address; this is an instruction to the judge. Slow the final sentence
> right down and stop dead on **lie**. Do not add anything after it. Do not smile it away.

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
