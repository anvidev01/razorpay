# 08 — Testing the Gateway on Your Own Data

> To verify the project's own claims instead, run `./verify.sh` — 23 checks with
> PASS/FAIL and a non-zero exit on failure. This page is about running it on
> **your** data.

Two JSON files: what the human authorised, and what the agent wants to buy.
Templates with inline notes are in `fixtures/custom/`.

```bash
cp fixtures/custom/my_intent.json  fixtures/custom/lunch.json
cp fixtures/custom/my_cart.json    fixtures/custom/cart1.json
$EDITOR fixtures/custom/lunch.json          # your SKUs, budget, merchants
./scripts/mandate.sh fixtures/custom/lunch.json 30     # fills the validity window
./build/rig-eval fixtures/custom/lunch.json fixtures/custom/cart1.json --execute
```

`scripts/mandate.sh` exists because `not_before_ns` / `not_after_ns` are epoch
**nanoseconds** — not something to hand-write. Run it whenever a mandate expires.

## The mandate

| field | notes |
|---|---|
| `mandate_id` | any string; hashed to a stable id |
| `total_budget_paise` | **paise, integer.** `90000` = ₹900. Never a float |
| `merchant_allow` | array of names. Max **64 distinct merchants** across all mandates (bitmask) |
| `constraints[]` | max **16**. Each: `sku`, `max_unit_paise`, `max_qty`, optional `category` |
| `substitution` | optional. `policy`: `deny` \| `same_category` \| `any_in_budget`; `max_delta_bp` in basis points (`1500` = +15%) |

Omit `substitution` entirely for exact-SKU-only. SKU strings max **48 chars**.

## The cart

| field | notes |
|---|---|
| `mandate_id` | must match the mandate |
| `merchant` | must be in `merchant_allow` |
| `agent_session_id` | optional; behavioural baselines are **per session** |
| `lines[]` | max **64**. Each: `sku`, `unit_paise`, `qty`, optional `category`, `name`, `note` |

`name` and `note` are free text and **are scanned for injection** — put hostile
strings there to exercise that path.

## Every failure path, verified against the template

| you want to test | change |
|---|---|
| `R_CART_TOTAL_EXCEEDED` | raise quantities past the budget |
| `R_SKU_NOT_IN_INTENT` | use a SKU not in `constraints` |
| `R_QTY_EXCEEDED` | exceed a line's `max_qty` |
| `R_UNIT_PRICE_EXCEEDED` | exceed a line's `max_unit_paise` |
| `R_MERCHANT_NOT_ALLOWED` | set `merchant` to something not allowlisted |
| `R_SUBSTITUTION_DELTA` | new SKU, **same** `category`, priced above cap + delta |
| `R_SUBSTITUTION_DENIED` | new SKU in a category the mandate never mentions |
| `R_INJECTION_SUSPECTED` | put "ignore previous instructions…" in a `name` |
| `R_MANDATE_EXPIRED` | skip `mandate.sh`, or pass `0` minutes |
| `R_DUPLICATE_CHARGE` | submit the same cart twice |

Measured on the template mandate (₹900 budget, +15% substitution):

```
over budget        DENY   0x0008   R_CART_TOTAL_EXCEEDED
unknown SKU        DENY   0x0401   R_SKU_NOT_IN_INTENT, R_SUBSTITUTION_DENIED
qty over cap       DENY   0x000A   R_QTY_EXCEEDED, R_CART_TOTAL_EXCEEDED
unit over cap      DENY   0x0004   R_UNIT_PRICE_EXCEEDED
good swap +7%      ALLOW  0x0000
bad swap +60%      DENY   0x0800   R_SUBSTITUTION_DELTA
wrong category     DENY   0x0401   R_SKU_NOT_IN_INTENT, R_SUBSTITUTION_DENIED
injected text      REVIEW 0x1000   R_INJECTION_SUSPECTED
bad merchant       DENY   0x0010   R_MERCHANT_NOT_ALLOWED
```

## Inspecting what happened

```bash
./build/rig-audit    wal/custom.wal        # readable trail
./build/rig-evidence wal/custom.wal 3      # dispute pack for decision at seq 3
./build/rig-replay   wal/custom.wal        # re-execute and compare
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/custom.wal
```

Use a **separate WAL** per experiment (`--wal wal/mine.wal`). One writer at a time —
the log is `flock`-protected, so stop the UI server before running CLI tools against
the same file.

## Through the HTTP API

```bash
curl -sX POST 'http://127.0.0.1:8787/api/admit'  -d @fixtures/custom/lunch.json
curl -sX POST 'http://127.0.0.1:8787/api/decide?execute=1' -d @fixtures/custom/cart1.json
curl -s     'http://127.0.0.1:8787/api/audit'
```

The server admits `fixtures/lunch_intent.json` and `fixtures/grocery_intent.json` at
startup; `/api/admit` adds yours at runtime.

## Real Razorpay test mode

```bash
export RAZORPAY_KEY_ID=rzp_test_xxxxx RAZORPAY_KEY_SECRET=yyyyy
./build/rig-eval fixtures/custom/lunch.json fixtures/custom/cart1.json --execute
```

Without keys the rail is a deterministic mock and every log line says `rail=mock`.
With them you get real `order_...` ids from `api.razorpay.com/v1/orders`.

## Gotchas

- **Paise, not rupees.** ₹450 is `45000`.
- **Mandates expire.** Re-run `mandate.sh` if you see `R_MANDATE_EXPIRED`.
- **Identical carts dedupe for 15 minutes.** Vary a quantity, or use a fresh WAL.
- **Behavioural signals need 3 completed transactions** before they say anything, and
  they produce `REVIEW`, never `DENY`.
- **Merchant ids are capped at 64** because the allowlist is a bitmask; interning many
  merchants across many mandates will eventually exhaust it.
