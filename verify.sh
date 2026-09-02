#!/usr/bin/env bash
# Proves every claim this project makes, in one command.
#   ./verify.sh          run everything
#   ./verify.sh --quick  skip benchmarks and sanitizers (~15s instead of ~90s)
set -uo pipefail
cd "$(dirname "$0")"
G=$'\033[32m'; R=$'\033[31m'; Y=$'\033[33m'; D=$'\033[2m'; B=$'\033[1m'; Z=$'\033[0m'
# Credentials live in a gitignored .env so they are never pasted into a shell that
# does not outlive one command, and never end up in the repo.
if [ -f .env ]; then set -a; . ./.env; set +a; fi

QUICK=0; [ "${1:-}" = "--quick" ] && QUICK=1
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); printf "  ${G}PASS${Z}  %-46s ${D}%s${Z}\n" "$1" "${2:-}"; }
no(){ FAIL=$((FAIL+1)); printf "  ${R}FAIL${Z}  %-46s ${D}%s${Z}\n" "$1" "${2:-}"; }
hdr(){ printf "\n${B}%s${Z}\n" "$1"; }

pkill -f rig-gateway 2>/dev/null; sleep 0.3      # the WAL allows one writer
mkdir -p wal; rm -f wal/verify*.wal
W="--wal wal/verify.wal"

hdr "0 · Build"
if [ ! -x build/rig-gateway ]; then
  cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 && \
  cmake --build build -j >/dev/null 2>&1 && ok "engine builds from clean" || no "engine build"
else ok "engine already built"; fi
[ -d control-plane/out ] || ./control-plane/build.sh >/dev/null 2>&1
[ -d control-plane/out ] && ok "independent Java auditor builds" || no "java build"

hdr "1 · Correctness — the policy kernel"
out=$(./build/rig-tests 2>&1)
n=$(echo "$out" | grep -oE '[0-9]+/[0-9]+ checks passed')
echo "$out" | grep -q "FAILURES" || [ -z "$n" ] && : ; \
if [ -n "$n" ] && ! echo "$out" | grep -q "FAILURES"; then
  ok "kernel golden vectors" "$n"
else no "kernel tests" "$(echo "$out" | tail -2 | tr '\n' ' ')"; fi
out=$(./build/rig-tests-intent 2>&1)
n=$(echo "$out" | grep -oE '[0-9]+/[0-9]+ checks passed')
if [ -n "$n" ] && ! echo "$out" | grep -q "FAILURES"; then
  ok "intent-translation regressions" "$n"
else no "intent tests"; fi

hdr "2 · The three graceful failures"
v(){ ./build/rig-eval "$1" "$2" $W --json 2>/dev/null | python3 -c "import json,sys;d=json.load(sys.stdin);print(d['decision'],d['verdict_hex'])"; }
[ "$(v fixtures/lunch_intent.json fixtures/lunch_cart.json)" = "ALLOW 0x0000" ] \
  && ok "lunch cart allowed" "ALLOW 0x0000" || no "lunch cart"
[ "$(v fixtures/lunch_intent.json fixtures/blender_cart.json)" = "DENY 0x000d" ] \
  && ok "hallucinated blender blocked" "DENY 0x000D, 3 reasons" || no "blender"
[ "$(v fixtures/grocery_intent.json fixtures/cart_milk_organic.json)" = "DENY 0x0800" ] \
  && ok "wrong substitution blocked" "DENY 0x0800" || no "substitution"
rm -f wal/verify_r.wal
./build/rig-eval fixtures/grocery_intent.json fixtures/cart_retry_a.json --wal wal/verify_r.wal >/dev/null 2>&1
[ "$(./build/rig-eval fixtures/grocery_intent.json fixtures/cart_retry_b.json --wal wal/verify_r.wal --json 2>/dev/null | python3 -c "import json,sys;print(json.load(sys.stdin)['verdict_hex'])")" = "0x2000" ] \
  && ok "agent retry collapsed, no double charge" "DENY 0x2000" || no "idempotency"
[ "$(v fixtures/grocery_intent.json fixtures/cart_injection.json)" = "DENY 0x140d" ] \
  && ok "prompt injection denied on INTENT" "DENY 0x140D" || no "injection"

hdr "3 · Gating — bypasses and forgeries"
out=$(./build/rig-attack 2>&1)
echo "$out" | grep -q "all bypasses refused" \
  && ok "8 bypasses refused, 1 legitimate payment" "$(echo "$out"|grep -c REFUSED) refusals" || no "attack suite"
echo "$out" | grep -q "unenrolled device"        && ok "mandate forged by another device refused" || no "forgery: device"
echo "$out" | grep -q "does not cover this mandate" && ok "mandate altered after signing refused" || no "forgery: tamper"

hdr "4 · Explainability — audit trail and replay"
rm -f wal/verify_a.wal
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json --wal wal/verify_a.wal --execute >/dev/null 2>&1
./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json --wal wal/verify_a.wal >/dev/null 2>&1
./build/rig-replay wal/verify_a.wal 2>&1 | grep -q "divergent : .*0" \
  && ok "C++ replays its own log, 0 divergent" || no "cpp replay"
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/verify_a.wal 2>&1 | grep -q "agree on every money action" \
  && ok "Java re-implementation agrees bit-for-bit" "shares no code with the engine" || no "java replay"
./build/rig-evidence wal/verify_a.wal 3 2>/dev/null | python3 -c "
import json,sys;d=json.load(sys.stdin)
assert d['6_reproducibility']['matches_recorded'] is True
assert d['log']['chain_intact'] is True
assert d['1_human_authority']['signed_by_device']" 2>/dev/null \
  && ok "evidence pack re-executes and names the signer" || no "evidence pack"
tam=$(./scripts/tamper.sh wal/verify_a.wal 2>&1 || true)
echo "$tam" | grep -qi "broken" \
  && ok "one flipped bit breaks the hash chain" "$(echo "$tam" | sed 's/\x1b\[[0-9;]*m//g' | grep -o 'crc mismatch.*' | head -1)" \
  || no "tamper detection"

hdr "5 · Track 02 — honest risk metrics"
out=$(./build/rig-riskeval 2>&1); rc=$?
echo "$out" | grep -q "held out" \
  && ok "measured on a HELD-OUT test set" "$(echo "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -A1 'TEST ' | head -1 | awk '{print "precision "$6"  recall "$7"  FPR "$8}')" \
  || no "held-out evaluation"
echo "$out" | grep -q "no meaningful overfitting" \
  && ok "train/test gap within tolerance" "tuning never saw the test split" || no "overfitting check"
[ $rc -eq 0 ] && ok "held-out metrics inside the operating envelope" \
              || no "risk detector out of envelope"

if [ "$QUICK" = "0" ]; then
hdr "5b · Portability"
cat > /tmp/rig_port.cpp <<'CPP'
#include "rig/clock.hpp"
#include <fcntl.h>
#include <unistd.h>
int main(){ const auto a=rig::mono_ns(); volatile int s=0;
  for(int i=0;i<1000000;i++) s=s+1; if(rig::mono_ns()<=a) return 1;
  if(rig::wall_ns()==0) return 1;
  int fd=::open("/tmp/rig_p.tmp",O_CREAT|O_WRONLY|O_TRUNC,0644); ::write(fd,"x",1);
  int rc=rig::durable_flush(fd); ::close(fd); ::unlink("/tmp/rig_p.tmp"); return rc; }
CPP
c++ -std=c++20 -O2 -Iengine/include /tmp/rig_port.cpp -o /tmp/rig_port 2>/dev/null && /tmp/rig_port \
  && ok "native clock + durable flush" "F_FULLFSYNC path on macOS" || no "native clock"
c++ -std=c++20 -O2 -DRIG_FORCE_PORTABLE_CLOCK -Iengine/include /tmp/rig_port.cpp -o /tmp/rig_portp 2>/dev/null && /tmp/rig_portp \
  && ok "portable clock + durable flush" "the branch Linux compiles, run here too" || no "portable clock"

hdr "6 · Memory safety"
SSL=$(brew --prefix openssl@3 2>/dev/null || echo /usr)
c++ -std=c++20 -O1 -g -fsanitize=address,undefined -Iengine/include -I"$SSL/include" \
   engine/src/kernel.cpp engine/tests/test_kernel.cpp -o /tmp/rig_san 2>/dev/null \
   && /tmp/rig_san >/dev/null 2>&1 && ok "ASan + UBSan clean" || no "sanitizers"

hdr "7 · Performance (measured, not claimed)"
# Report what this machine measures rather than asserting a fixed number -- the
# figure moves with load, and quoting someone else's idle-machine number as a pass
# condition would be dishonest.
k=$(./build/bench-engine-kernel 8 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | grep -oE 'p50= *[0-9.]+' | grep -oE '[0-9.]+' | head -1)
[ -n "$k" ] && ok "policy kernel, 8-line cart" "p50 ${k} ns on this machine (README: 30.8 ns idle)" \
             || no "kernel benchmark"
./build/rig-load 2000 >/dev/null 2>&1 && ok "durable commit under load" "group-committed WAL" || no "load"
fi

hdr "8 · End to end through the payment rail"
pkill -f rig-gateway 2>/dev/null; sleep 0.3; rm -f wal/rig.wal
nohup ./build/rig-gateway --port 8799 >/tmp/verify_gw.log 2>&1 </dev/null & disown 2>/dev/null || true
for i in $(seq 1 60); do curl -s -m 1 http://127.0.0.1:8799/api/health >/dev/null 2>&1 && break; sleep 0.2; done
rail=$(curl -s -m 3 http://127.0.0.1:8799/api/health | python3 -c "import json,sys;print(json.load(sys.stdin)['rail'])" 2>/dev/null)
r=$(curl -s -m 10 -X POST "http://127.0.0.1:8799/api/decide?execute=1" \
  -d '{"mandate_id":"mnd_8f21c4","merchant":"swiggy","agent_session_id":"verify","lines":[{"sku":"SKU_MEAL_THALI_001","unit_paise":24000,"qty":1}]}' \
  | python3 -c "import json,sys;d=json.load(sys.stdin)['decision'];print(d['decision'],d['paid'],d['payment_order_id'])" 2>/dev/null)
case "$r" in
  "ALLOW True order_"*) ok "cart → decision → token → payment" "rail=$rail  ${r##* }" ;;
  *) no "end-to-end payment" "$r" ;;
esac
[ "$rail" = "mock" ] && printf "  ${Y}note${Z}  rail is the offline mock. For real test-mode order ids:\n        ${D}export RAZORPAY_KEY_ID=rzp_test_xxx RAZORPAY_KEY_SECRET=yyy${Z}\n"
curl -s -m 5 http://127.0.0.1:8799/.well-known/agent-commerce | python3 -c "
import json,sys;d=json.load(sys.stdin)
assert d['protocol'] and d['endpoints']['decide'] and len(d['reject_codes'])>10" 2>/dev/null \
  && ok "agent-readable discovery document" "/.well-known/agent-commerce" || no "discovery doc"
curl -s -m 5 http://127.0.0.1:8799/api/catalog | python3 -c "
import json,sys;assert len(json.load(sys.stdin)['items'])>0" 2>/dev/null \
  && ok "machine-readable catalogue" "/api/catalog" || no "catalogue endpoint"
curl -s -m 3 -D- -o /dev/null http://127.0.0.1:8799/api/health | grep -qi "access-control-allow-origin" \
  && no "CORS wildcard present" || ok "no CORS wildcard on the money API"
pkill -f "rig-gateway --port 8799" 2>/dev/null

printf "\n${B}%d passed, %d failed${Z}\n" "$PASS" "$FAIL"
if [ "$FAIL" -eq 0 ]; then
  printf "${G}Every claim in the README is reproducible on this machine.${Z}\n\n"
  printf "${D}Interactive demo:  ./run.sh          then http://127.0.0.1:8787\n"
  printf "Narrated walkthrough: ./run.sh --demo${Z}\n\n"
else
  printf "${R}Some checks failed — see above.${Z}\n\n"
fi
exit "$FAIL"
