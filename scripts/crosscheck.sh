#!/usr/bin/env bash
# Differential test: generate adversarial carts and require the C++ kernel and the
# independent Java auditor to reach the SAME verdict on every one.
#
#   ./scripts/crosscheck.sh [seed] [count]
#
# This exists because the two implementations silently disagreed once. The C++ kernel
# was fixed to aggregate quantity across cart lines; the Java auditor was left checking
# per line. "Zero divergences" stayed true only because no logged decision exercised the
# split-quantity case -- the exact bypass the fix was written for.
#
# One hand-written regression is not enough to trust a dual-implementation claim. This
# sweeps every control: unknown SKUs, substitution categories, price and quantity caps,
# recurring tails, disallowed merchants, overflow-scale amounts and empty lines.
set -uo pipefail
cd "$(dirname "$0")/.."

SEED="${1:-7}"; COUNT="${2:-120}"
G=$'\033[32m'; R=$'\033[31m'; D=$'\033[2m'; B=$'\033[1m'; Z=$'\033[0m'
WAL="wal/crosscheck.wal"; rm -f "$WAL"

printf "\n${B}cross-check${Z}  ${D}C++ kernel vs independent Java auditor · seed %s · %s carts${Z}\n\n" \
       "$SEED" "$COUNT"

python3 - "$SEED" "$COUNT" "$WAL" <<'PY'
import json, random, subprocess, sys
seed, count, wal = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
rng = random.Random(seed)
SKUS = ["SKU_MEAL_THALI_001","SKU_DRINK_LIME_007","SKU_SIDE_RAITA_014",
        "SKU_MEAL_BIRYANI_02","SKU_DRINK_COLA_003","SKU_SIDE_PAPAD_021",
        "SKU_APPLIANCE_BLENDER_5","SKU_UNKNOWN_XYZ"]
CATS = ["FOOD_MAIN","FOOD_DRINK","FOOD_SIDE","","ELECTRONICS"]
made = 0
for i in range(count):
    lines = []
    for _ in range(rng.choice([1,1,2,3,5,8,12])):
        ln = {"sku": rng.choice(SKUS),
              "unit_paise": rng.choice([1,100,4500,24000,30000,60000,600000]),
              "qty": rng.choice([1,1,2,3,5,40])}
        if rng.random() < 0.4: ln["category"] = rng.choice(CATS)
        if rng.random() < 0.15: ln["recurring_paise"] = rng.choice([0,9900,99900])
        lines.append(ln)
    json.dump({"mandate_id":"mnd_8f21c4","merchant":rng.choice(["swiggy","zomato","evil_corp"]),
               "agent_session_id":"xc%d"%i,"lines":lines}, open("/tmp/xc_cart.json","w"))
    r = subprocess.run(["./build/rig-eval","fixtures/lunch_intent.json","/tmp/xc_cart.json",
                        "--wal",wal,"--json"], capture_output=True, text=True)
    if r.returncode in (0,1) and r.stdout.strip().startswith("{"): made += 1
print("  %d decisions logged" % made)
PY

strip(){ sed 's/\x1b\[[0-9;]*[A-Za-z]//g'; }
div(){ eval "$1" 2>&1 | strip | grep -oE 'divergent *: *[0-9]+' | grep -oE '[0-9]+$' | head -1; }

CPP=$(div "./build/rig-replay $WAL")
JAV=$(div "java -cp control-plane/out com.razorpay.rig.ReplayAuditor $WAL")
printf "  C++  self-replay      divergences: %s\n" "${CPP:-?}"
printf "  Java independent      divergences: %s\n" "${JAV:-?}"

if [ "${CPP:-1}" = "0" ] && [ "${JAV:-1}" = "0" ]; then
  printf "\n  ${G}${B}PASS${Z}  both kernels agree on every generated cart\n\n"; rm -f "$WAL"; exit 0
fi
printf "\n  ${R}${B}FAIL${Z}  the implementations disagree — inspect %s\n\n" "$WAL"; exit 1
