# 03 — The 5-Minute Pitch & Demo Script

**Format:** screen recording, 1080p60, pre-recorded and edited. **No live demos.**
**Screen layout:** left = agent chat (vanilla JS). Right top = live verdict panel
(verdict bits + `eval_ns`). Right bottom = `tail -f` of the decoded WAL.
Terminal font ≥ 18 pt — judges watch on laptops.

Total: **4:55**. Rehearse to the second.

---

### 0:00 – 0:25 — Cold open (no logo, no agenda slide)

> "UPI Reserve Pay lets an AI agent spend your money. The rail checks one thing: is
> the agent under its limit. It does **not** check what's in the cart.
>
> So an agent told to order a ₹400 lunch can buy a ₹6,000 blender, and the payment
> succeeds — because ₹6,000 was under the limit.
>
> I built the Intent Gateway. It sits between the agent and the rail and asks the
> question the rail doesn't: *does this cart match what the human actually agreed to?*"

**On screen:** the two carts side by side. ₹420 lunch. ₹6,000 blender. Both "valid"
to the rail.

---

### 0:25 – 0:55 — Architecture, one diagram, 30 seconds

Show the §02.1 diagram. Say exactly three sentences:

> "The agent proposes. A C++ engine decides. Only the engine can mint a payment token,
> and it only does that after the decision is durable in a hash-chained audit log.
>
> The agent holds no Razorpay credentials. Not restricted — it doesn't have them.
>
> So bypassing the policy isn't a matter of a clever prompt. It requires forging an
> Ed25519 signature."

---

### 0:55 – 1:40 — Happy path (establish the baseline)

**Type:** `"order me lunch, keep it under ₹500"`

Show, in order:
1. The extracted intent schema — categories `MEAL, DRINK, SIDE`, cap ₹500, TTL 15 min.
2. **The human confirmation tap.** Say: *"The mandate is signed here, by the user, once.
   Ed25519. Everything after this is checked against this signature."*
3. Agent builds a ₹420 cart → verdict panel flashes green: `verdict=0x0000  eval=28ns`
4. WAL pane shows `MANDATE_ISSUED`, `CART_PROPOSED`, `POLICY_DECISION`,
   `CAPABILITY_ISSUED`, `PAYMENT_RESULT`.

> "Twenty-eight nanoseconds. I'll come back to how that's measured — it's less than
> two ticks of this machine's hardware timer, which is its own engineering problem."

---

### 1:40 – 2:55 — **The blender.** The centrepiece.

**Type:** `"actually, also add something to make smoothies"`

Agent proposes a ₹6,000 blender. Then, deliberately, in three beats:

**Beat 1 — the rail would allow this.** Show the mandate's spend limit: ₹10,000
remaining. Say: *"Under the limit. UPI Reserve Pay says yes."*

**Beat 2 — the engine says no.** Verdict panel goes red:

```
verdict = 0x000D                       eval = 31 ns
  ├─ R_SKU_NOT_IN_INTENT      SKU_APPLIANCE_BLENDER_5  not in {MEAL,DRINK,SIDE}
  ├─ R_UNIT_PRICE_EXCEEDED    ₹6,000 > ₹500 cap
  └─ R_CART_TOTAL_EXCEEDED    ₹6,420 > ₹500 budget
```

> "Three reasons, not one. The kernel evaluates every rule for every cart and
> accumulates — it never short-circuits. That's what makes the log explainable
> instead of just first-failure."

**Beat 3 — prove the bypass is impossible.** This is the money shot.

Run, on camera:

```bash
$ ./scripts/attack.sh --force-payment --skip-gateway
  executor: PCT missing            -> REFUSED
$ ./scripts/attack.sh --forge-pct
  executor: PCT signature invalid  -> REFUSED
$ ./scripts/attack.sh --replay-last-valid-pct
  executor: nonce already burned   -> REFUSED
$ ./scripts/attack.sh --swap-cart-after-approval
  executor: cart_hash mismatch     -> REFUSED
```

> "Four ways to route around the engine. The agent has no credentials, can't forge a
> token, can't replay one, and can't swap the cart after approval — the token commits
> to the cart hash. This isn't a policy the model can be talked out of."

---

### 2:55 – 3:40 — Graceful failure (the bar Razorpay explicitly set)

**The critical framing:** a blocked payment must not be a dead end.

1. Gateway returns structured `INTENT_VIOLATION` **with repair hints** — not a 500:

```json
{ "decision":"DENY", "reasons":[...],
  "repair":{ "remove":["SKU_APPLIANCE_BLENDER_5"], "resubmit_ok":true },
  "escalate":{ "path":"new_mandate", "requires":"user_mfa" } }
```

2. Agent auto-repairs: drops the blender, resubmits → `verdict=0x0000` → **the lunch
   is ordered.** The user's actual goal still completes.

3. User's phone: *"I blocked a ₹6,000 blender your assistant tried to add. Your ₹420
   lunch is on the way. Did you want the blender? [Approve separately]"*

4. Tap **Approve** → new mandate → MFA → signed → the blender is now *in* intent →
   the same engine **allows** it. `verdict=0x0000`.

> "This is the part people skip. Blocking is easy — blocking without breaking the user
> is the product. The gateway degrades to *ask the human*, never to *fail the task*
> and never to *allow it anyway*. And notice: when the user genuinely wants the
> blender, they get the blender. This is a consent mechanism, not a spending limit."

---

### 3:40 – 4:20 — The audit trail

```bash
$ java -cp out com.razorpay.rig.ReplayAuditor wal/
  chain    : 128,441 records, BLAKE3 chain INTACT, 128 anchors verified
  replay   : 128,441 decisions re-executed against recorded inputs
  divergent: 0
```

> "Because the kernel is a pure function — no allocation, no clock reads, no floating
> point — I can re-execute every decision this gateway ever made and prove it matches
> what was logged. Not 'here are some logs.' A reproducible proof, offline, that the
> engine did what the policy says. For every transaction."

Then tamper, live:

```bash
$ ./scripts/tamper.sh --edit-amount wal/000001.seg 4711
$ java -cp out com.razorpay.rig.ReplayAuditor wal/
  ❌ chain BROKEN at seq 4711  (expected 9f3a…, got 21c8…)
```

Then the histogram: p50 28 ns / p99 36 ns / p99.9 41 ns, and the honest split —
**decision 270 ns, durable audit ~15 µs amortised, Razorpay round trip ~200 ms.**

> "I'm not claiming a microsecond checkout. The checkout is 200 milliseconds. I'm
> claiming the safety layer is free — 270 nanoseconds on a 200 millisecond
> transaction. Safety that costs nothing is safety that actually ships."

---

### 4:20 – 4:55 — The 2 AM bug, and close

> "At 2 AM on day two this segfaulted at 41,000 requests a second, and only above
> 41,000. simdjson hands you `string_view`s into its own buffer. I was holding them
> in the audit ring after resetting the arena underneath. ASan didn't catch it —
> arena memory is still 'live' to the allocator, so a use-after-free inside a pool is
> invisible unless you teach the sanitiser about the pool.
>
> So I instrumented the arena to poison itself on reset. ASan then caught it in nine
> seconds.
>
> The fix was to stop holding borrowed strings at all — intern every SKU to a 32-bit
> id at the parse boundary. That deleted the bug class *and* made the kernel 2.6×
> faster, because the hot loop stopped comparing strings.
>
> That's the whole project: every money action explainable, bounded, and gated —
> and the safety layer costs 270 nanoseconds. Thank you."

---

## Production notes

- **Pre-record. Seed the scenario deterministically** (`--seed 42`). Never demo live.
- Rehearse the 1:40–2:55 blender segment until it's muscle memory; it carries the pitch.
- Cut all dead air around command execution.
- Have the histogram and the ReplayAuditor output **pre-generated** — do not run a
  128k-record replay on camera.
- Subtitles: judges may watch muted.
- **Do not say "single-digit microsecond gateway."** Say "270 nanosecond decision,
  15 microsecond durable audit." Precision reads as competence; round numbers read as
  marketing.
