#!/usr/bin/env bash
# Run YOUR OWN scenario through the gateway. No JSON, no fixtures.
#
#   ./scripts/try.sh
#       interactive: it asks what the human authorises, then what the agent
#       tries to buy, and shows the verdict.
#
#   ./scripts/try.sh "order me lunch under 500 rupees" \
#       SKU_MEAL_THALI_001:240:1  SKU_DRINK_LIME_007:60:2
#       one-shot: mandate sentence, then SKU:RUPEES:QTY for each cart line.
#
#   ./scripts/try.sh --sector saas "buy 10 standard seats under 12000 rupees" \
#                    SKU_SEAT_STANDARD:900:10
#       one of the shipped industries: food retail saas travel procurement subscription
#   ./scripts/try.sh --catalog my_products.json "buy 2 licences under 40000" ...
#       your own product feed. See fixtures/catalog.json for the shape.
#
# The point: the mandate and the cart come from different places, exactly as they
# would in production -- a human authorises one thing, an agent proposes another,
# and the kernel decides whether the second is inside the first.
set -uo pipefail
cd "$(dirname "$0")/.."
B=$'\033[1m'; D=$'\033[2m'; Z=$'\033[0m'; G=$'\033[32m'; R=$'\033[31m'; Y=$'\033[33m'; C=$'\033[36m'

CATALOG="fixtures/catalog.json"
# The engine has no notion of any industry -- prove it by switching the product feed
# and changing nothing else. --sector is shorthand for one of the shipped catalogues.
if [ "${1:-}" = "--sector" ]; then
  SECTOR="${2:?--sector needs a name}"
  CATALOG="fixtures/catalogs/$SECTOR.json"
  if [ ! -f "$CATALOG" ]; then
    printf "unknown sector: %s\navailable: " "$SECTOR"
    ls fixtures/catalogs/*.json 2>/dev/null | xargs -n1 basename | sed 's/.json//' | tr '\n' ' '
    printf "\n"; exit 1
  fi
  shift 2
elif [ "${1:-}" = "--catalog" ]; then CATALOG="${2:?--catalog needs a file}"; shift 2; fi
[ -f "$CATALOG" ] || { printf "${R}no such catalogue: %s${Z}\n" "$CATALOG"; exit 1; }

UTTER="${1:-}"; shift 2>/dev/null || true
LINES=("$@")

show_catalog(){
  printf "\n${B}What this merchant sells${Z} ${D}(%s)${Z}\n" "$CATALOG"
  python3 - "$CATALOG" <<'PY'
import json,sys
for i in json.load(open(sys.argv[1]))['items']:
    print(f"   {i['sku']:<26} {i['name']:<22} Rs {i['price_rupees']:>6}   {i.get('category','')}")
PY
}

if [ -z "$UTTER" ]; then
  show_catalog
  printf "\n${B}1. What is the human authorising?${Z} ${D}(plain English)${Z}\n"
  printf "${D}   e.g. order me lunch, a thali and a drink, under 500 rupees${Z}\n${C}> ${Z}"
  IFS= read -r UTTER </dev/tty
  [ -z "$UTTER" ] && { printf "${Y}nothing entered${Z}\n"; exit 1; }
fi

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
printf "\n${B}2. Drafting the mandate${Z} ${D}(the agent proposes; nothing is signed yet)${Z}\n"
./build/rig-intent "$UTTER" --out "$TMP/intent.json" --catalog "$CATALOG" || {
  printf "${Y}The translator refused, which is the correct behaviour: it will not invent\n"
  printf "a SKU that this merchant does not sell. Try wording closer to the catalogue,\n"
  printf "or pass --catalog with your own product feed.${Z}\n"; exit 1; }

if [ ${#LINES[@]} -eq 0 ]; then
  show_catalog
  printf "\n${B}3. What does the AGENT actually try to buy?${Z}\n"
  printf "${D}   One line at a time:  SKU  PRICE_IN_RUPEES  QTY${Z}\n"
  printf "${D}   Try something OUTSIDE the mandate -- that is the interesting case.${Z}\n"
  printf "${D}   Blank line when done.${Z}\n"
  while :; do
    printf "${C}> ${Z}"; IFS= read -r l </dev/tty || break
    [ -z "$l" ] && break
    set -- $l
    [ $# -ne 3 ] && { printf "${Y}   need three values: SKU PRICE QTY${Z}\n"; continue; }
    LINES+=("$1:$2:$3")
  done
fi
[ ${#LINES[@]} -eq 0 ] && { printf "${Y}no cart lines${Z}\n"; exit 1; }

MID=$(python3 -c "import json;print(json.load(open('$TMP/intent.json'))['mandate_id'])")
python3 - "$TMP/cart.json" "$MID" "${LINES[@]}" <<'PY'
import json,sys
out, mid, raw = sys.argv[1], sys.argv[2], sys.argv[3:]
lines=[]
for r in raw:
    p=r.split(':')
    if len(p)!=3: sys.exit(f"bad line '{r}' -- expected SKU:RUPEES:QTY")
    sku,rupees,qty=p
    lines.append({"sku":sku,"unit_paise":int(round(float(rupees)*100)),"qty":int(qty)})
json.dump({"mandate_id":mid,"merchant":"swiggy",
           "agent_session_id":"judge_try","lines":lines}, open(out,'w'), indent=2)
PY
[ $? -ne 0 ] && exit 1

printf "\n${B}4. The cart the agent proposed${Z}\n"
python3 - "$TMP/cart.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); t=0
for l in d['lines']:
    sub=l['unit_paise']*l['qty']; t+=sub
    print(f"   {l['sku']:<26} Rs {l['unit_paise']/100:>8.2f} x {l['qty']:<3} = Rs {sub/100:>9.2f}")
print(f"   {'':<26} {'':>11}   {'total':<5}  Rs {t/100:>9.2f}")
PY

printf "\n${B}5. The verdict${Z} ${D}(deterministic kernel, every rule checked)${Z}\n"
rm -f wal/try.wal
./build/rig-eval "$TMP/intent.json" "$TMP/cart.json" --wal wal/try.wal
rc=$?

printf "${D}  the mandate and the full audit record are in wal/try.wal${Z}\n"
printf "${D}  inspect:  ./build/rig-audit wal/try.wal${Z}\n"
printf "${D}  replay:   ./build/rig-replay wal/try.wal${Z}\n\n"
exit $rc
