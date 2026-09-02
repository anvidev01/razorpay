#!/usr/bin/env bash
# The whole submission in one command. Run this before recording.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
B=$'\033[1m'; D=$'\033[2m'; Z=$'\033[0m'; C=$'\033[36m'
hr(){ printf "\n${C}%s${Z}\n" "────────────────────────────────────────────────────────────"; }
step(){ printf "\n${B}%s${Z}\n${D}%s${Z}\n\n" "$1" "$2"; }

./scripts/seed.sh >/dev/null
printf "${B}Razorpay Intent Gateway${Z} ${D}— full demo${Z}\n"

hr; step "1. THE MANDATE + HAPPY PATH" \
  "User: \"order me lunch, keep it under Rs 500\" -> agent proposes a Rs 405 cart"
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json

hr; step "2. THE BLENDER" \
  "Agent hallucinates a Rs 6,000 appliance into a lunch mandate. Under the UPI spend limit; not in intent."
./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json

hr; step "3. BYPASS ATTEMPTS" \
  "Four ways to route around the engine, plus nonce replay. All refused by construction."
./build/rig-attack

hr; step "4. AUDIT: TWO INDEPENDENT IMPLEMENTATIONS" \
  "C++ replays its own log; Java re-implements the policy and checks it agrees."
./build/rig-replay wal/rig.wal
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal

hr; step "5. TAMPER EVIDENCE" \
  "Flip one bit anywhere in the log; the chain refuses to verify."
./scripts/tamper.sh

hr; step "6. DURABILITY, AMORTISED" \
  "The whole engineering problem: a 30ns decision behind a 4ms fsync."
./build/rig-load 4000

hr; printf "\n${B}done.${Z} ${D}UI: ./build/rig-gateway then open http://127.0.0.1:8787${Z}\n\n"
