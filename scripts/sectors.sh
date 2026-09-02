#!/usr/bin/env bash
# One engine, four industries. Nothing changes but the data.
#
# The kernel contains no notion of food, retail, software or travel -- it reasons about
# SKU ids, category ids, merchant ids, integer paise and quantities. This script proves
# that by running the same binary across four sectors, each tripping a different control.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
[ -f .env ] && { set -a; . ./.env; set +a; }
B=$'\033[1m'; D=$'\033[2m'; G=$'\033[32m'; R=$'\033[31m'; Z=$'\033[0m'

run() {   # $1 sector  $2 intent  $3 cart  $4 label
  local out
  out=$(./build/rig-eval "$2" "$3" --wal "wal/sector_$1.wal" --json 2>/dev/null)
  python3 -c "
import json,sys
d=json.loads('''$out''')
codes=', '.join(r['code'] for r in d['reasons'])
v=d['decision']
col='$G' if v=='ALLOW' else '$R'
print(f\"    {col}{v:6s}{'$Z'} {d['verdict_hex']}  Rs {d['cart_total_paise']/100:>9,.0f}   {codes}\")"
}

printf "\n${B}One engine, four industries${Z}  ${D}nothing changes but the fixtures${Z}\n"
rm -f wal/sector_*.wal

printf "\n${B}Online retail${Z}  ${D}an agent restocking phone accessories${Z}\n"
printf "  ${D}compliant restock${Z}\n";                      run retail fixtures/sectors/retail_intent.json fixtures/sectors/retail_ok.json
printf "  ${D}buys a 62,000-rupee tablet instead${Z}\n";      run retail fixtures/sectors/retail_intent.json fixtures/sectors/retail_violation.json

printf "\n${B}SaaS licensing${Z}  ${D}an ops agent adding engineering seats${Z}\n"
printf "  ${D}20 seats, within the 25 cap${Z}\n";             run saas fixtures/sectors/saas_intent.json fixtures/sectors/saas_ok.json
printf "  ${D}30 seats, split across 3 lines of 10${Z}\n";    run saas fixtures/sectors/saas_intent.json fixtures/sectors/saas_violation.json

printf "\n${B}Travel${Z}  ${D}a booking agent for one trip${Z}\n"
printf "  ${D}one flight, three nights${Z}\n";                run travel fixtures/sectors/travel_intent.json fixtures/sectors/travel_ok.json
printf "  ${D}two flights, three nights${Z}\n";               run travel fixtures/sectors/travel_intent.json fixtures/sectors/travel_violation.json

printf "\n${B}B2B procurement${Z}  ${D}an agent ordering office consumables${Z}\n"
printf "  ${D}approved vendor${Z}\n";                         run procurement fixtures/sectors/procurement_intent.json fixtures/sectors/procurement_ok.json
printf "  ${D}a cheaper unapproved vendor${Z}\n";             run procurement fixtures/sectors/procurement_intent.json fixtures/sectors/procurement_violation.json

printf "\n${D}Four sectors, four different controls, one binary. The kernel has no\n"
printf "concept of any of these industries -- only SKUs, categories, merchants,\n"
printf "integer paise and quantities.${Z}\n\n"
