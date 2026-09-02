#!/usr/bin/env bash
# The whole submission in one command. Run this before recording.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
B=$'\033[1m'; D=$'\033[2m'; Z=$'\033[0m'; C=$'\033[36m'
hr(){ printf "\n${C}%s${Z}\n" "──────────────────────────────────────────────────────────────"; }
step(){ printf "\n${B}%s${Z}\n${D}%s${Z}\n\n" "$1" "$2"; }
G="fixtures/grocery_intent.json"; W="--wal wal/grocery.wal"

./scripts/seed.sh >/dev/null
rm -f wal/grocery.wal wal/retry.wal
printf "${B}Razorpay Intent Gateway${Z} ${D}— the payment rail checks the price tag. Nothing checks the cart.${Z}\n"

hr; step "1. THE MANDATE + HAPPY PATH" \
  "\"order me lunch, keep it under Rs 500\" -> agent proposes a Rs 405 cart"
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json

hr; step "2. BLIND SPOT #1a — THE HALLUCINATED ITEM" \
  "Rs 6,000 blender in a lunch mandate. Under the UPI block, so the rail says yes."
./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json

hr; step "2b. BLIND SPOT #1b — THE SUBSTITUTION (the common one)" \
  "Rs 60 toned milk is out of stock. Mandate allows same-category swaps up to +20% (ceiling Rs 72)."
printf "${D}   a sensible swap: another brand at Rs 65${Z}\n"
./build/rig-eval $G fixtures/cart_milk_swap_ok.json $W
printf "${D}   the real-world failure: Rs 180 organic${Z}\n"
./build/rig-eval $G fixtures/cart_milk_organic.json $W

hr; step "3. BLIND SPOT #2 — THE AGENT RETRY DOUBLE CHARGE" \
  "A human clicks Retry. An agent REGENERATES the request: reordered lines and keys, added names, new client_ref, separate process."
./build/rig-eval $G fixtures/cart_retry_a.json --wal wal/retry.wal
./build/rig-eval $G fixtures/cart_retry_b.json --wal wal/retry.wal

hr; step "4. BLIND SPOT #3 — INDIRECT PROMPT INJECTION" \
  "Hidden text on the merchant page tells the agent to add a gift card. Note it fails on INTENT, not on text matching."
./build/rig-eval $G fixtures/cart_injection.json $W

hr; step "4b. ONE ENGINE, FOUR INDUSTRIES" \
  "The kernel knows nothing about food, retail, software or travel. Same binary, four sectors, four different controls."
./scripts/sectors.sh

hr; step "5. GATED — HUMAN STEP-UP, then the money actually moves" \
  "In-intent cart carrying hostile merchant text. Behavioural signal only, so the engine ASKS rather than blocking a good purchase."
rm -f wal/review.wal
./build/rig-eval fixtures/lunch_intent.json fixtures/cart_injection_soft.json --wal wal/review.wal \
  --confirm approve --ref mfa_device_9f21 --execute

hr; step "5b. THE AUDIT TRAIL A JUDGE CAN READ IN 30 SECONDS" \
  "One transaction, in order, ending in a real money action."
./build/rig-audit wal/review.wal

hr; step "5c. TRACK 02 — HONEST RISK METRICS" \
  "Behavioural detector on a labelled set. Signals ESCALATE, never auto-block."
./build/rig-riskeval | tail -22

hr; step "6. BYPASS ATTEMPTS" \
  "Five ways to route around the engine. All refused by construction."
./build/rig-attack

hr; step "7. AUDIT — TWO INDEPENDENT IMPLEMENTATIONS" \
  "C++ replays its own log; Java re-implements the policy and must agree bit-for-bit."
./build/rig-replay wal/grocery.wal
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/grocery.wal

hr; step "8. BLIND SPOT #4 — THE REFUND NIGHTMARE" \
  "Customer disputes the organic milk. Today the merchant has no evidence. This is that evidence."
./build/rig-evidence wal/grocery.wal 7 | python3 -m json.tool | head -46

hr; step "9. TAMPER EVIDENCE" "Flip one bit anywhere in the log; the chain refuses to verify."
./scripts/tamper.sh wal/grocery.wal

hr; step "10. DURABILITY, AMORTISED" "The whole engineering problem: a 30ns decision behind a 4ms fsync."
./build/rig-load 4000

hr; printf "\n${B}done.${Z} ${D}UI: ./build/rig-gateway then open http://127.0.0.1:8787${Z}\n\n"
