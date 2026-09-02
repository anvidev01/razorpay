# 03 — The 5-Minute Recording Script

**Track 01 · AI Growth & Agentic Commerce**

Read this out loud. Every number in it is checked by `./verify.sh` (40/40) — if a
claim here ever stops being true, verification fails before the video does.

**Setup:** left = UI at `http://127.0.0.1:8787`, right = terminal at ~18pt. 1080p60.

```bash
./scripts/seed.sh     # fresh WAL, mandate windows refreshed
./verify.sh           # must read 40 passed, 0 failed
./run.sh              # header must read `rail razorpay-test`, NOT `mock`
```

Click one scenario **before** recording — the first Razorpay call pays a TLS
handshake (~176 ms vs ~69 ms warm).

| | segment | ends |
|---|---|---|
| 0:00 | The gap in agentic payments | 0:30 |
| 0:30 | The mandate — what a human actually signs | 1:00 |
| 1:00 | Happy path, live on Razorpay | 1:35 |
| 1:35 | Three ways agents lose money | 2:25 |
| 2:25 | The subscription trap, and asking a human | 2:55 |
| 2:55 | Try to get around it | 3:25 |
| 3:25 | One engine, five industries | 3:50 |
| 3:50 | Prove it after the fact | 4:25 |
| 4:25 | Numbers, and the bug I found in myself | 5:00 |

---

## 0:00 – 0:30 · The gap

> "**Agentic commerce** is shipping. NPCI is rolling out **agentic payments on UPI**;
> Razorpay announced **Agent Pay**. Google has **AP2**, OpenAI has **ACP**. Every one of
> them answers the same two questions: is this a **registered agent**, and is it under
> the **transaction limit**.
>
> Both necessary. Neither one looks at the cart.
>
> So when an **autonomous agent** orders a six-thousand-rupee blender instead of a
> four-hundred-rupee lunch — six thousand is under the ten-thousand limit, the agent is
> registered, and **every system in the chain approves it.**
>
> I built the layer that reads the cart."

---

## 0:30 – 1:00 · The mandate

*Compose your own order → type it live.*

```
order me lunch, a thali and a drink, under 500 rupees
```

> "The **LLM drafts**. It does not **authorise**. What comes back is a **scoped
> mandate** in plain English — two items, these **price ceilings**, this **budget**,
> these merchants, expires in an hour. I read it, and **I** sign it.
>
> Signed with **Ed25519**, on the user's device. The gateway is enrolled with **only the
> public half — it cannot sign a mandate at all.** That's the difference between a
> shared secret and a real signature: a system that can verify but not forge."

*Press **Sign & admit**. Point at `HUMAN · once`.*

> "**Once.** Not per purchase. That's what makes it **agentic** and not just checkout
> with extra steps."

---

## 1:00 – 1:35 · Happy path

*Send to gateway.*

> "**Thirty-one nanoseconds** to decide — a **deterministic, branch-free policy kernel**,
> no allocation, no floats, integer paise only. Four milliseconds to make that decision
> **durable**. Then the real call to **Razorpay Orders API**: sixty-nine milliseconds.
>
> The check costs thirty-one nanoseconds. The payment it guards costs sixty-nine
> milliseconds. **The safety layer is two million times faster than the thing it
> protects. It is free.**"

*Point at the audit panel.*

> "Mandate signed. Cart proposed. Decision. **Capability token** minted. Payment
> attempted. Paid — a real **order id from Razorpay test mode**. Every money action, in
> order, with its reason. And the token is minted **only after the decision is durable**
> — you can never have a payment this log doesn't explain."

---

## 1:35 – 2:25 · Three ways agents lose money

*Scripted scenarios tab.*

**`2 · Hallucinated item`**

> "**DENY**, `0x000D`. **Three reasons at once** — the kernel never stops at the first
> failure, so you get the whole story, not the first complaint. And **no capability token
> exists**, so this cart physically cannot reach the rail."

**`3 · Auto-repair`**

> "The agent drops the bad line and resubmits. **The lunch still arrives.** Blocking
> isn't the product — blocking *without breaking the user's goal* is. That's the
> difference between a **guardrail** and an outage."

**`4 · Retry storm`**

> "Checkout times out. A human retries with the same bytes. An **agent regenerates** —
> reordered lines, new client reference. Every rail sees a brand-new order and **charges
> twice**. We key on the **canonical cart hash**, so it collapses. **Semantic
> idempotency.** One charge."

---

## 2:25 – 2:55 · Subscriptions, and asking a human

**`6 · Hidden subscription`**

> "One rupee today. **Nine hundred ninety-nine every month after.** The cart total is one
> rupee — under budget, correct merchant, real SKU. **Every limit-based check passes.**
> We deny it `0x400000`, because the mandate authorised a purchase, not a **recurring
> commitment**."

**`5 · Hidden instructions`**

> "**Prompt injection** on the merchant page. Every item is inside the mandate — only the
> text is hostile. So this is **REVIEW**, not DENY."

*Press **Approve · MFA**.*

> "A behavioural signal is **probabilistic**. Auto-blocking a guess turns every false
> positive into a lost sale. Here it costs one tap — **human-in-the-loop**, recorded, and
> bound to this decision **and** this cart hash. That binding is what a **chargeback**
> turns on."

---

## 2:55 – 3:25 · Try to get around it

*Terminal: `./build/rig-attack` — let the wall of REFUSED sit for two seconds.*

> "Eight ways to move money without permission. **All eight refused. Exactly one
> legitimate payment authorised.**"

*Read three:*

> "Sign the mandate with a **different device** — refused, unenrolled key. **Raise the
> budget after signing** — refused, the signature covers the exact bytes. **Skip the
> gateway entirely** — there's no token, and the executor accepts nothing else. Replay a
> used one — **nonce already burned**.
>
> The agent holds **no Razorpay credentials at any point.** Getting around this isn't a
> prompt-engineering problem. It's forging Ed25519."

---

## 3:25 – 3:50 · One engine, five industries

*Terminal: `./scripts/sectors.sh`*

> "Same binary, five sectors, five different controls. **Online retail** — a sixty-two
> thousand rupee tablet in an accessories restock, denied. **SaaS** — thirty seats split
> across three lines against a twenty-five seat cap; **aggregate quantity**, because the
> agent chooses how many lines it sends. **Travel** — second flight breaks the budget.
> **B2B procurement** — unapproved vendor. **Subscriptions** — the trap you just saw.
>
> **Nothing changed but the fixtures.** There is not one food concept in the kernel.
> This is **payments infrastructure**, not a shopping demo."

---

## 3:50 – 4:25 · Prove it afterwards

```bash
./build/rig-replay wal/rig.wal
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal
```

> "The C++ engine **replays its own log** from recorded inputs. Then a **Java
> re-implementation that shares no code with it** replays the same log and agrees bit for
> bit. **Zero divergences.** Two independent implementations of the same policy."

*`./scripts/tamper.sh` — flip one bit.*

> "**Hash-chained, tamper-evident write-ahead log.** Change one bit anywhere and the
> chain refuses to verify. A database row you can edit quietly; this you cannot."

*UI → **View evidence pack**.*

> "For a **dispute**: what the human approved, which device signed it, what the agent
> proposed, what was decided and why — and section six **re-runs the decision and
> confirms it still matches**. Today the merchant absorbs these losses because none of
> this exists."

---

## 4:25 – 5:00 · The numbers, and the bug

> "On a **held-out test set** — split by session, thresholds tuned on train only —
> **precision 0.908, false-positive rate 0.002.** Against an aggressive blocker, the
> gateway nets **fifty thousand four hundred forty-five rupees ahead**, because in
> payments a **false positive is a lost sale**, not a free win.
>
> And I audited my own engine and found a real bug. **Quantity caps were enforced
> per line** — and the *agent* decides how many lines it sends. So ten lines of one item
> bought ten of a max-one item, and it returned **ALLOW**. Fixed, and pinned in the tests.
>
> **That bug is the argument for the entire project.** The layer above the kernel drifts.
> So the kernel has to check the cart.
>
> Clone it and run `./verify.sh` — **forty checks, and it exits non-zero if any of this
> was a lie.**"

---

## If a judge asks "is the Razorpay integration real?"

```bash
./scripts/prove-razorpay.sh
```

Creates an order, then **reads it back out of Razorpay's API** with `mandate_id`,
`decision_id` and `wal_seq` in the notes — and shows a denied cart creates **no order at
all**. Reading the record back off their servers is the part nothing local can fake.

Say out loud: **only three of the seven steps ever contact Razorpay.** The denials, the
duplicate and the step-up never do. That's the point, and the UI says so explicitly.

## Recording notes

- **Rehearse the hex codes.** `0x000D` blender · `0x0800` substitution · `0x2000`
  duplicate · `0x1000` injection · `0x400000` subscription. From memory, it reads as
  fluency.
- **Don't narrate the UI.** Say what it *means*, not what it shows.
- `./scripts/seed.sh` before every take.
- Mid-scenario fumble → press **Reset demo**, don't restart.

## If you only get 90 seconds

Blender denied with three reasons → hidden subscription denied → `rig-attack` →
both auditors agreeing. That is the thesis: **bounded, gated, explainable.**

## Keyword coverage

Track 01 language this script says out loud, for a skim-reading judge:

agentic commerce · agentic payments on UPI · NPCI · Agent Pay · AP2 · ACP · autonomous
agent · registered agent · transaction limit · scoped mandate · Ed25519 · asymmetric
signing · capability token · human-in-the-loop · guardrail · prompt injection · recurring
commitment · semantic idempotency · canonical cart hash · deterministic policy kernel ·
branch-free · integer paise · durable · hash-chained · tamper-evident · write-ahead log ·
deterministic replay · zero divergences · held-out test set · precision · false-positive
rate · chargeback · dispute evidence · Razorpay Orders API · test mode · payments
infrastructure · bounded, gated, explainable
