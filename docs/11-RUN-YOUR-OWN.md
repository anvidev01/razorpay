# 11 — Run Your Own Scenario

Everything below works from a terminal. Nothing here uses the shipped fixtures, so
you are testing the engine, not a rehearsed demo.

## The fastest path

```
./scripts/try.sh
```

It asks two questions — **what the human authorises**, then **what the agent tries to
buy** — and shows the verdict. No JSON.

The two inputs are deliberately separate. That is the whole product: a human approves
one thing, an agent proposes another, and the kernel decides whether the second is
inside the first.

## One-shot form

```
./scripts/try.sh "order me lunch under 500 rupees" \
    SKU_MEAL_THALI_001:240:1  SKU_DRINK_LIME_007:60:1
```

Cart lines are `SKU:RUPEES:QTY`. Try changing a price, a quantity, or a SKU that was
never authorised, and watch which rule fires.

## Your own sector

The catalogue is the merchant's product feed, not part of the engine. Point at your own:

```
./scripts/try.sh --catalog fixtures/catalog_saas.json \
    "buy 10 standard seats under 12000 rupees" \
    SKU_SEAT_STANDARD:900:10
```

`fixtures/catalog_saas.json` is a four-item example — copy its shape, put your products
in it, and the same binary enforces your mandates. The kernel contains no notion of any
industry.

## Things worth trying

| type this | what should happen |
|---|---|
| a cart matching the mandate | `ALLOW`, capability token minted |
| a price above the per-item cap | `DENY 0x0004` |
| a SKU that was never authorised | `DENY`, and the line is named |
| the same quantity split across several lines | `DENY 0x0002` — caps aggregate |
| a total over budget | `DENY 0x0008` |
| the same cart twice | second one collapses, no double charge |

The kernel never stops at the first violation, so a bad cart returns **all** of its
reasons at once, with per-line attribution.

## Driving the pieces directly

```
./build/rig-intent "buy 10 seats under 12000 rupees" \
    --out /tmp/m.json --catalog fixtures/catalog_saas.json
```

Then write a cart JSON:

```json
{ "mandate_id": "mnd_cli", "merchant": "swiggy",
  "lines": [ { "sku": "SKU_SEAT_STANDARD", "unit_paise": 90000, "qty": 10 } ] }
```

Money is **integer paise** everywhere — `unit_paise: 90000` is ₹900. There are no
floats in this system.

```
./build/rig-eval /tmp/m.json /tmp/cart.json --wal wal/try.wal
./build/rig-eval /tmp/m.json /tmp/cart.json --json      # machine-readable
```

Then inspect what it recorded:

```
./build/rig-audit  wal/try.wal      # every money action, with reasons
./build/rig-replay wal/try.wal      # re-execute from recorded inputs
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/try.wal
```

The last one is a Java re-implementation sharing no code with the C++ engine. It must
agree on every decision.

## If the translator refuses

```
Cannot draft a mandate: nothing in this merchant's catalogue matches that request
not in this catalogue: laptops
```

That is correct behaviour, not a failure. It will not invent a SKU the merchant does
not sell. Use `--catalog` with your own feed, or word the request closer to the
catalogue.

### Typos

A typo in a **filler** word is tolerated — `3 drinks undeer 300 rupees` drafts
correctly. Misspelling `under` cannot change what is bought, only the sentence
scaffolding around it.

A typo in a **product** name is refused, with a hint:

```
not in this catalogue: mojtio
"mojtio" -> did you mean "mojito"?
```

The hint is never applied. You retype it. That line is deliberate: guessing which
product someone meant is precisely how "mojito" once became a lime soda in this
codebase, and the human stays the one who decides what is being bought.

**The translator is not the security boundary.** It drafts; a human signs. If it drafts
something wrong, the human does not sign it. Read the drafted mandate before judging a
verdict — a surprising `DENY` is usually a mandate tighter than you expected, and the
engine enforcing it exactly.

## Proving the payment rail is real

```
./scripts/prove-razorpay.sh
```

Creates an order through the gateway, then reads it back **out of Razorpay's API** with
`mandate_id`, `decision_id` and `wal_seq` in the notes, and shows that a denied cart
creates no order at all. It starts its own gateway if one is not running.

Needs `RAZORPAY_KEY_ID` / `RAZORPAY_KEY_SECRET` in a gitignored `.env`. Test keys only —
it refuses a live key.

## Checking every claim at once

```
./verify.sh
```

40 checks. It exits non-zero if any claim in the README is false on your machine.
