# 10 — Everything From the Terminal

No browser required. One rule first:

> **The WAL takes a single writer.** Stop the UI server before running CLI tools against
> the same log — `pkill -f rig-gateway` — or point the CLI at its own file with
> `--wal wal/mine.wal`. The tools refuse rather than corrupt the chain.

Load credentials once per shell (both scripts do this for you):

```bash
set -a; . ./.env; set +a
```

---

## The 60-second version

```bash
./verify.sh                 # 25 checks, PASS/FAIL, non-zero exit on failure
./run.sh --demo             # the whole narrated walkthrough
./scripts/prove-razorpay.sh # creates an order, reads it back OUT of Razorpay
```

---

## Plain English → signed mandate → real payment

```bash
./build/rig-intent "order me two thalis and a lime soda under 800 rupees" \
      --out /tmp/mandate.json
```

```
Understood  2 x Veg thali up to Rs 264 each, 1 x Fresh lime soda up to Rs 66 each,
            total under Rs 800
This is a DRAFT. Nothing is signed until a human approves it.
mandate written to /tmp/mandate.json  valid 30 min
```

Review that file — that is the human-approval step — then spend against it:

```bash
cat > /tmp/cart.json <<'JSON'
{ "mandate_id":"mnd_cli", "merchant":"swiggy", "agent_session_id":"sess_cli",
  "lines":[ {"sku":"SKU_MEAL_THALI_001","category":"FOOD_MAIN","unit_paise":24000,"qty":2},
            {"sku":"SKU_DRINK_LIME_007","category":"FOOD_DRINK","unit_paise":6000,"qty":1} ] }
JSON

./build/rig-eval /tmp/mandate.json /tmp/cart.json --wal wal/cli.wal --execute
```

```
ALLOW  verdict = 0x0000   eval = 834 ns   wal_seq = 3
cart total 54000 paise (Rs 540.00)   durable in 4794 us
capability issued nonce=1 bound to cart f8c08584109cde08...
payment rail=razorpay-test status=200 PAID order_TXCO69zaTSveoy
```

Swap the blender in and it is refused with every reason, and **no order is created**:

```
DENY  verdict = 0x040D
  |- R_SKU_NOT_IN_INTENT      item is not in the approved intent
  |- R_UNIT_PRICE_EXCEEDED    unit price exceeds the per-item cap
  |- R_CART_TOTAL_EXCEEDED    cart total exceeds the mandate budget
  |- R_SUBSTITUTION_DENIED    substitute is not in an approved category
```

---

## Forging a mandate by hand

The **Run attacks** button in the UI does this, but you can reproduce it yourself, which
is the version a sceptical evaluator will want.

The user's device key lives in `wal/device.key` (gitignored, 0600). It persists across
restarts, so a signature made by `rig-sign` matches what the gateway enrolled.

```bash
# a mandate to sign
./build/rig-intent "two thalis under 800 rupees" --out /tmp/mand.json

# 1. the ENROLLED device -- accepted
./build/rig-sign /tmp/mand.json                    # prints the two headers
G=$(./build/rig-sign /tmp/mand.json 2>/dev/null)
curl -s -X POST http://127.0.0.1:8787/api/admit \
     -H "$(echo "$G" | sed -n 1p)" -H "$(echo "$G" | sed -n 2p)" \
     --data-binary @/tmp/mand.json
# -> {"ok":true,"error":""}

# 2. a DIFFERENT key -- refused
F=$(./build/rig-sign /tmp/mand.json --forge 2>/dev/null)
curl -s -X POST http://127.0.0.1:8787/api/admit \
     -H "$(echo "$F" | sed -n 1p)" -H "$(echo "$F" | sed -n 2p)" \
     --data-binary @/tmp/mand.json
# -> {"ok":false,"error":"mandate signed by an unenrolled device (8c6c4c22...),
#                        expected 104023bb..."}

# 3. edit the mandate AFTER signing -- refused
sed -i '' 's/"total_budget_paise":80000/"total_budget_paise":99999999/' /tmp/mand.json
curl -s -X POST http://127.0.0.1:8787/api/admit \
     -H "$(echo "$G" | sed -n 1p)" -H "$(echo "$G" | sed -n 2p)" \
     --data-binary @/tmp/mand.json
# -> {"ok":false,"error":"signature does not cover this mandate (altered after signing?)"}
```

Capture `$F` **once** — every `--forge` run mints a fresh throwaway key, so signing
twice and comparing fingerprints will confuse you.

`/api/admit` verifies whatever signature you supply and never signs on your behalf when
you do. Omit the headers and it falls back to the local device, which is a demo
convenience only — a real phone is remote and always sends its own signature.

---

## Every command

| what | command |
|---|---|
| Verify all claims | `./verify.sh` · `--quick` skips benchmarks/sanitizers |
| Narrated walkthrough | `./run.sh --demo` |
| Prove Razorpay is real | `./scripts/prove-razorpay.sh` |
| Draft a mandate from English | `./build/rig-intent "..." --out m.json` |
| Sign a mandate as the device | `./build/rig-sign m.json` · `--forge` for a bad key |
| Decide one cart | `./build/rig-eval <intent> <cart> --wal W [--execute]` |
| Step-up (REVIEW path) | `... --confirm approve --ref mfa_device_9f21 --execute` |
| Machine-readable decision | `... --json` |
| Readable audit trail | `./build/rig-audit W` |
| Dispute evidence pack | `./build/rig-evidence W <decision_seq>` |
| Replay (C++) | `./build/rig-replay W` |
| Replay (independent Java) | `java -cp control-plane/out com.razorpay.rig.ReplayAuditor W` |
| Bypasses + forged mandates | `./build/rig-attack` |
| Risk confusion matrix | `./build/rig-riskeval` |
| Tamper detection | `./scripts/tamper.sh W` |
| Kernel benchmark | `./build/bench-engine-kernel 8` |
| Durability under load | `./build/rig-load 4000` |
| Test suites | `./build/rig-tests` · `./build/rig-tests-intent` |
| Raw WAL records | `./build/wal-dump W` |

`W` is a WAL path, e.g. `wal/cli.wal`.

---

## Driving the HTTP API directly

Start the server (`./run.sh`) and ignore the browser:

```bash
# decide, and execute if allowed
curl -s -X POST 'http://127.0.0.1:8787/api/decide?execute=1' -d @/tmp/cart.json | python3 -m json.tool

# plain English -> draft mandate
curl -s -X POST http://127.0.0.1:8787/api/intent -d '{"utterance":"two meals under 900"}'

# admit a mandate at runtime
curl -s -X POST http://127.0.0.1:8787/api/admit -d @/tmp/mandate.json

# answer a REVIEW
curl -s -X POST http://127.0.0.1:8787/api/confirm \
     -d '{"decision_id":3,"approved":true,"ref":"mfa_device_9f21"}'

# audit trail, evidence pack, reset
curl -s http://127.0.0.1:8787/api/audit           | python3 -m json.tool
curl -s 'http://127.0.0.1:8787/api/evidence?seq=3' | python3 -m json.tool
curl -s -X POST http://127.0.0.1:8787/api/reset
```

---

## Which commands actually contact Razorpay

Only an **ALLOW that you execute**. Everything else is deliberately local:

| | rail contacted |
|---|---|
| `rig-eval ... --execute` on an ALLOW | **yes** — a real `order_...` |
| `rig-eval` without `--execute` | no |
| any DENY, DUPLICATE, or REVIEW | **no** — the refusal is the point |
| `rig-attack`, `rig-riskeval`, `rig-replay`, `rig-audit` | no |

If you want to be sure the key is live, `./scripts/prove-razorpay.sh` creates an order
and then reads it back off Razorpay's servers with your `wal_seq` in its notes.
