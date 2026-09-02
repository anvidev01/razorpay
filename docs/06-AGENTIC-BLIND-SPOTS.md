# 06 — The Four Blind Spots, and Whether They Are Real

Before building any of this I checked whether the problem is real or invented.
It is real, it is current, and in one case the infrastructure it targets was
announced by Razorpay itself. Sources at the end.

---

## 0. The premise checks out

NPCI is preparing a **Unified Agent Protocol (UAP)** for agentic payments on UPI, built
on two existing mechanisms: **UPI Circle** (delegating payment authority to a secondary
party such as an agent) and **Reserve Pay** (blocking funds for multiple debits). Banks
currently cap those blocks at **₹10,000 for up to 90 days** — which is exactly the
"₹10,000 wallet" this project assumes.

And the target is not hypothetical: at the India AI Impact Summit on 20 Feb 2026,
**Razorpay and NPCI announced Agentic Payments on Claude**, in pilot with Zomato,
Swiggy and Zepto.

The rail checks *is this a registered agent* and *is the amount within the block*.
Both are necessary. Neither looks at the cart.

---

## 1. Blind to WHAT is bought — the intent gap

**Real.** Documented in agentic-commerce attack analysis: a shopping agent visits a
site, indirect prompt injection reprograms its context, and *"when the agent constructs
the final checkout payload, it includes unauthorized items."* The rail approves it
because the arithmetic is fine.

The subtler version is not an attack at all — it is **substitution**. Grocery inventory
turns hourly. Half a cart can go out of stock between browsing and checkout, so agents
substitute. Industry guidance is explicit that agents *"need substitution rules in
machine-readable form, such as same SKU other size, same category competing brand"* —
and that getting it wrong means *"charging customers for something they don't want."*

A ₹6,000 blender for a ₹400 lunch is the loud failure. **₹180 organic milk for ₹60
toned milk is the common one**, and no spending limit catches it.

### What the gateway does

`SubstPolicy` on the signed mandate, with three settings:

| policy | meaning |
|---|---|
| `SUBST_DENY` | exact SKUs only |
| `SUBST_SAME_CATEGORY` | same category, within `max_delta_bp` of the approved cap |
| `SUBST_ANY_IN_BUDGET` | any item, still bounded by every price cap |

"Deny every substitution" is unusable in grocery; "allow anything under the limit" is
how ₹60 milk becomes ₹180 milk. The middle setting is the useful one.

```
mandate: SKU_MILK_TONED_1L, category DAIRY_MILK, cap ₹60, substitution +20%
         => ceiling ₹72

₹65 different-brand milk   ALLOW  0x0000   substituted_for = SKU_MILK_TONED_1L
₹180 organic milk          DENY   0x0800   R_SUBSTITUTION_DELTA
₹40 beer (category ALCOHOL) DENY  0x0400   R_SUBSTITUTION_DENIED
```

The organic milk is **not** reported as an unknown item. It is reported as a legitimate
substitution that costs too much — which is the difference between a useful audit trail
and a confusing one.

---

## 2. Blind to HOW agents retry — the double charge

**Real, and well documented.** *"With agent payments, retries are normal. Agents hit
network timeouts, lose connections before they get a response, and sometimes restart
mid-job."* An autonomous agent *"has no spinner. It has a retry loop with exponential
backoff, and that retry loop is the failure mode."*

The specific trap: a human clicking Retry re-sends the same bytes. An agent handling a
timeout **re-generates** the request — different key order, different whitespace, a
display name added, a fresh client-side id, lines in a different order. A syntactic
matcher sees a new order and charges twice. It compounds with sub-agents, where each
one retries independently.

### What the gateway does

The idempotency key is derived from the **canonical interned cart**, not the JSON text:

```
key = SHA256(mandate_id ‖ cart_hash ‖ merchant_id ‖ amount_paise)
```

`cart_hash` is computed over interned `uint32` SKU ids with **line order normalised**,
so every cosmetic difference collapses. Measured, across two separate OS processes:

```
attempt 1  cart_retry_a.json   ALLOW  0x0000   capability issued, wal_seq 3
attempt 2  cart_retry_b.json   DENY   0x2000   R_DUPLICATE_CHARGE -> decision #3
           (reordered lines, reordered keys, added names, new client_ref)
```

The retry window is **rebuilt from the WAL on startup**, so an agent retrying after the
gateway restarts still cannot produce a second charge. And the repair hint for a
duplicate says `"action": "stop_retrying"` rather than "remove items and resubmit" —
telling a retrying agent to retry differently is how you build a retry storm.

---

## 3. Hidden instructions — indirect prompt injection

**Real, and ranked #1.** OWASP places prompt injection at the top of its LLM risk list.
Attackers hide instructions in merchant pages using CSS to keep them invisible to humans
while leaving them in the DOM for agents, and embed them in JSON-LD, where structured
fields are treated as high-signal context.

### What the gateway does — and the honest limit

Two layers, and it matters which one is load-bearing:

**The control (structural).** A cart is evaluated against the **signed intent mandate**.
Text on a merchant page cannot add a SKU to a mandate the user signed earlier. An
injected gift card fails on `R_SKU_NOT_IN_INTENT` whether or not anything noticed the
text.

**The telemetry (heuristic).** A scanner at the parse boundary flags instruction-shaped
text and invisible Unicode, raising `R_INJECTION_SUSPECTED` so the *attempt* is visible
in the audit log.

> **The scanner is not the security boundary and this project does not claim it is.**
> Heuristic filtering of injected instructions is unreliable by construction — an
> attacker who knows the patterns writes around them. That is precisely why OWASP ranks
> injection first: there is no dependable text-level filter. Detection makes the attempt
> auditable; **intent-binding is what actually stops it.**

Measured on `fixtures/cart_injection.json`:

```
verdict 0x140D
  R_SKU_NOT_IN_INTENT     <- the control: not in the signed mandate
  R_UNIT_PRICE_EXCEEDED
  R_CART_TOTAL_EXCEEDED
  R_SUBSTITUTION_DENIED
  R_INJECTION_SUSPECTED   <- the telemetry: an attempt was made, and logged
```

Strip the injection scanner entirely and this cart is still denied.

---

## 4. The refund nightmare — the evidence gap

**Real, and currently unresolved.** *"AI agent chargeback liability is unresolved…
Today, the merchant absorbs the loss by default."* When an agent buys, *"the evidence
you would normally use to fight a chargeback largely disappears. There is no human click
trail. No behavioral session data from the cardholder. The device fingerprint belongs to
the agent's MCP server, not the buyer's device."*

Card networks are responding with agent-specific trails (Visa agent tokens,
Mastercard Agent Pay), and **Google's AP2** — announced Sept 2025 with 60+ partners
including Mastercard, PayPal and Amex — defines **Intent Mandate → Cart Mandate →
Payment Mandate**, each a signed credential, together forming *"a non-repudiable audit
trail of who authorized what, within what limits."*

**This gateway independently arrived at the same three-artefact structure**, which is a
good sign for the design:

| AP2 | this project |
|---|---|
| Intent Mandate | `IntentSchema`, Ed25519-signed at admission |
| Cart Mandate | `CART_PROPOSED` + canonical `cart_hash` |
| Payment Mandate | the Payment Capability Token, binding the cart hash |

### What the gateway does

`rig-evidence <wal> <seq>` emits the record that is otherwise missing, answering six
questions a dispute actually turns on:

```
1_human_authority     what did the human approve?  (mandate, budget, TTL, Ed25519)
2_agent_proposal      what did the agent try to buy? (raw cart as submitted)
3_policy_decision     what was decided, and why?     (verdict bits + reasons)
4_authorisation       was a capability actually issued?
5_tamper_evidence     could this have been altered?  (prev_hash / this_hash)
6_reproducibility     does the verdict follow from the inputs?  <- re-executed live
```

Section 6 is the one that matters. The pack does not merely quote the log — it
**re-runs the decision** from the recorded inputs and reports whether the result matches
what was recorded. And the whole log can be re-verified by an independent Java
implementation that shares no code with the engine.

That is the difference between "our logs say the customer approved it" and
"here is a reproducible proof, checkable by a third party, that this purchase followed
from an instruction the customer cryptographically signed."

---

## Sources

- [Razorpay & NPCI: Agentic Payments for UPI on Claude](https://razorpay.com/blog/agentic-payments-and-npci/)
- [India may allow agentic AI-led UPI transactions under new NPCI protocol — Business Standard](https://www.business-standard.com/finance/news/india-may-allow-agentic-ai-led-upi-transactions-under-new-npci-protocol-126070801343_1.html)
- [Announcing Agent Payments Protocol (AP2) — Google Cloud](https://cloud.google.com/blog/products/ai-machine-learning/announcing-agents-to-payments-ap2-protocol)
- [AP2 specification](https://ap2-protocol.org/specification/)
- [Nothing Clears Twice: Why Agent Payments Break on Retries — OrcaRouter](https://www.orcarouter.ai/blog/agent-to-agent-payments-idempotency)
- [Idempotent AI Agents: Retry-Safe Patterns for Production](https://www.buildmvpfast.com/blog/idempotent-ai-agent-retry-safe-patterns-production-workflow-2026)
- [Fooling AI Agents: Web-Based Indirect Prompt Injection Observed in the Wild — Unit 42](https://unit42.paloaltonetworks.com/ai-agent-prompt-injection/)
- [Who's Really Shopping? Retail Fraud in the Age of Agentic AI — Unit 42](https://unit42.paloaltonetworks.com/retail-fraud-agentic-ai/)
- [Indirect Prompt Injection Targets AI Agents — Zscaler ThreatLabz](https://www.zscaler.com/blogs/security-research/indirect-prompt-injection-web-content-targets-ai-agents)
- [AI Agent Chargeback Liability: Who Pays & How to Prepare — Chargeflow](https://www.chargeflow.io/blog/ai-agent-chargeback-liability)
- [Agentic Commerce Chargebacks and the Evidence Gap — Justt](https://justt.ai/blog/solving-agentic-commerce-chargebacks/)
- [Chargebacks in agentic commerce — Checkout.com](https://www.checkout.com/blog/chargebacks-in-agentic-commerce-how-merchants-can-stay-ahead)
- [Can AI solve e-grocery's erratic out-of-stock substitutions? — RetailWire](https://retailwire.com/discussion/can-ai-solve-e-grocerys-erratic-out-of-stock-substitutions/)
- [AI Shopping for Grocery: 2026 Retailer Playbook — Paz.ai](https://www.paz.ai/for/grocery-brands)

---

## 5. Blind to WHAT IT COMMITS TO — the recurring tail

A cart total bounds a **one-off** cost. A subscription's real cost is unbounded, and it
sits outside the total entirely.

```
mandate : one-off software licence, up to Rs 500, no subscriptions
cart    : "Design Pro -- 1 rupee first month"   unit_paise 100
                                                recurring_paise 99900

cart total       Rs 1.00      <- under every cap, under the budget
real commitment  Rs 999/mo    <- nowhere in the total
```

Every price check passes. The budget check passes. A limit-only rail approves it, and so
would any gateway that reasons purely about the amount charged today.

**The control:** a recurring commitment must be authorised *on its own terms*.

```json
"recurring": { "allow": false, "max_per_interval_paise": 0 }
```

Absent from a mandate, this defaults to **deny** — because the safe reading of "spend up
to ₹500" is a purchase, not a standing order.

| outcome | code |
|---|---|
| the mandate authorised no recurring charge at all | `R_SUBSCRIPTION_UNDISCLOSED` |
| recurring charge above the authorised per-interval ceiling | `R_RECURRING_EXCEEDS` |

A mandate that *does* permit subscriptions bounds them: `allow: true` with
`max_per_interval_paise: 50000` passes a ₹450/month plan and refuses a ₹999/month one —
reported as over-ceiling rather than undisclosed, because those are different failures.

The verdict also reports `recurring_paise` separately from `cart_total_paise`, so the
number that is *not* in the total is visible next to the one that is.

**Why this belongs with the other four.** It is the same shape as the blender: the money
moved is not the money authorised. The difference is that here the gap is in *time*
rather than in the basket, which is why a cart-total check alone cannot see it.
