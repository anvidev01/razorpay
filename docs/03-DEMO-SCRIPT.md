# 03 — The 5-Minute Pitch

Two windows, side by side. **Left**: the UI at `http://127.0.0.1:8787`.
**Right**: a terminal, font large enough to read on a phone.

**Before you record**

```bash
cp .env.example .env        # put the real Razorpay test key in it
./verify.sh                 # 25 checks; if anything fails, do not record yet
./run.sh                    # header must read `rail razorpay-test`, not `mock`
```

If the header says `mock`, the key is not loading — a judge will notice, and it
undercuts the one claim Track 01 asks for by name.

---

## 0:00 – 0:35 · The problem

> "NPCI is rolling out agentic payments on UPI. Razorpay and NPCI announced it on
> Claude in February — Zomato, Swiggy, Zepto. The rail blocks up to ten thousand
> rupees and checks two things: is this a registered agent, and is it under the
> limit.
>
> Both necessary. Neither looks at the cart.
>
> So if your agent orders a six-thousand-rupee blender instead of a four-hundred-rupee
> lunch, six thousand is less than ten thousand — and every system in the chain
> approves it."

*On screen: the hero line, "bounded by what the human agreed to".*

---

## 0:35 – 1:05 · What the human actually authorises

*Compose your own order → type it live.*

```
order me lunch, a thali and a drink, under 500 rupees
```

> "The assistant drafts. It does **not** authorise. I read one plain-English sentence
> back — two items, these ceilings, this budget — and **I** press sign. The signing key
> is on the user's device. The gateway holds only the public half and cannot sign a
> mandate at all."

*Press **Sign & admit**. Point at the `HUMAN · once` badge.*

> "Once. Not per purchase."

---

## 1:05 – 1:45 · The happy path, and the receipt

*Send to gateway.*

> "Thirty-one nanoseconds to decide. Four milliseconds to make that decision durable —
> deciding is instant, remembering is the slow part, and remembering is the point."

*Point at the audit panel filling in.*

> "Mandate signed. Cart proposed. Decision. Token minted. Payment attempted. Paid —
> that's a real order id from Razorpay test mode. Every money action, in order, with
> the reason."

---

## 1:45 – 2:40 · Three failures, handled

*Scripted scenarios tab.*

**Blender** — `2 · Hallucinated item`

> "`DENY`, `0x000D`. Three reasons at once — the kernel never stops at the first, so
> you get the whole story. And no token exists, so this cart *cannot* reach the rail."

**Auto-repair** — `3`

> "The agent drops it and resubmits. **The lunch still arrives.** Blocking isn't the
> product; blocking without breaking the user's actual goal is."

**Retry storm** — `4`

> "Checkout times out. A human clicks retry — same bytes. An agent **regenerates** the
> request: reordered lines, new client ref. Every rail sees a new order and charges
> twice. We key on the canonical cart, so it collapses. One charge."

---

## 2:40 – 3:20 · When it isn't sure, it asks

*Scenario `5 · Hidden instructions`.*

> "Hidden text on the merchant page. Every item here is inside the mandate — only the
> text is hostile. So this is `REVIEW`, not `DENY`."

*Point at the step-up card.*

> "A behavioural signal is probabilistic. Auto-blocking on a guess turns every false
> positive into a lost sale. Here it costs one tap."

*Press **Approve · MFA**.*

> "Human answer recorded, bound to this decision **and** this cart hash. Token minted.
> Paid. That binding is what a chargeback turns on."

---

## 3:20 – 4:00 · Try to get around it

*Terminal: `./build/rig-attack`*

> "Eight ways to move money without permission."

*Let the list land, then read three:*

> "Forge the mandate with another device — refused, wrong key. Raise the budget after
> signing — refused, the signature covers the exact bytes. Skip the gateway entirely —
> there is no token, and the executor accepts nothing else.
>
> The agent holds no Razorpay credentials. Getting around this isn't a prompt
> engineering problem. It's forging Ed25519."

---

## 4:00 – 4:35 · Prove it afterwards

*Terminal:*

```bash
./build/rig-replay wal/rig.wal
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal
```

> "The C++ engine replays its own log. Then a **Java re-implementation that shares no
> code with it** replays the same log and agrees bit for bit. Zero divergences."

*`./scripts/tamper.sh` — one bit.*

> "Flip one bit anywhere and the chain refuses to verify."

*UI: **View evidence pack**.*

> "For a dispute: what the human approved, which device signed it, what the agent
> proposed, what was decided and why — and section six **re-runs the decision** and
> confirms it matches. Today the merchant absorbs these losses because none of that
> exists."

---

## 4:35 – 5:00 · Close

> "Thirty-one nanoseconds on a two-hundred-millisecond checkout. The safety layer is
> free.
>
> I audited my own engine and found a real one: quantity caps were enforced per line,
> and the *agent* chooses how many lines it sends — so ten lines of one bought ten of a
> max-one item. Fixed, and pinned in the tests. That bug is the argument for the whole
> project: the layer above the kernel drifts, so the kernel has to check the cart.
>
> Clone it and run `./verify.sh`. Twenty-five checks, and it exits non-zero if any of
> this was a lie."

---

## Recording notes

- **Rehearse the verdict numbers.** `0x000D` blender, `0x0800` substitution,
  `0x2000` duplicate, `0x1000` injection. Saying them from memory reads as fluency.
- **Do not narrate the UI.** Say what it *means*, not what it shows.
- **Let `rig-attack` sit on screen** for two seconds before speaking. The wall of
  REFUSED is the strongest single frame in the video.
- `./scripts/seed.sh` before each take, so the log starts empty.
- If a take goes wrong mid-scenario, press **Reset demo** rather than restarting.
- 1080p60, terminal at ~18pt.

## If you only get 90 seconds

Blender denied with three reasons → step-up approved and paid → `rig-attack` →
both auditors agreeing. That is the whole thesis: **bounded, gated, explainable.**
