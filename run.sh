#!/usr/bin/env bash
# One command to build and run the Intent Gateway.
#   ./run.sh            build if needed, start the UI on :8787, open a browser
#   ./run.sh --demo     run the full CLI walkthrough instead
#   ./run.sh --port N   use a different port
set -uo pipefail
cd "$(dirname "$0")"
B=$'\033[1m'; G=$'\033[32m'; R=$'\033[31m'; D=$'\033[2m'; Z=$'\033[0m'
PORT=8787; DEMO=0
while [ $# -gt 0 ]; do
  case "$1" in
    --demo) DEMO=1 ;;
    --port) PORT="${2:-8787}"; shift ;;
    -h|--help) echo "usage: ./run.sh [--demo] [--port N]"; exit 0 ;;
  esac
  shift
done

need() { command -v "$1" >/dev/null 2>&1; }
miss=""
need cmake  || miss="$miss cmake"
need c++    || miss="$miss a C++20 compiler"
need python3|| miss="$miss python3"
if [ -n "$miss" ]; then
  printf "${R}missing:${Z}%s\n\n" "$miss"
  echo "  macOS:  xcode-select --install && brew install cmake simdjson openssl@3"
  echo "  Debian: sudo apt install build-essential cmake libsimdjson-dev libssl-dev libcurl4-openssl-dev default-jdk"
  exit 1
fi

if [ ! -x build/rig-gateway ]; then
  printf "${B}building the engine${Z} ${D}(first run only, ~30s)${Z}\n"
  cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release >/dev/null || {
    printf "\n${R}cmake failed.${Z} Most often a missing dependency:\n"
    echo "  brew install simdjson openssl@3        # macOS"
    exit 1; }
  cmake --build build -j >/dev/null || { printf "\n${R}build failed${Z}\n"; exit 1; }
  printf "  ${G}built${Z}\n"
fi

if need javac && [ ! -d control-plane/out ]; then
  printf "${B}building the independent Java auditor${Z}\n"
  ./control-plane/build.sh >/dev/null 2>&1 && printf "  ${G}built${Z}\n" \
    || printf "  ${D}skipped (optional -- the C++ auditor still runs)${Z}\n"
fi

if [ "$DEMO" = "1" ]; then exec ./scripts/demo.sh; fi

mkdir -p wal
pkill -f "rig-gateway --port $PORT" 2>/dev/null; sleep 0.3
rm -f wal/rig.wal                       # start every run from a clean audit log
./scripts/seed.sh >/dev/null 2>&1 || true

if [ -z "${RAZORPAY_KEY_ID:-}" ]; then
  printf "\n${D}No Razorpay test keys set, so the payment rail is a deterministic mock.\n"
  printf "For real test-mode order ids:\n"
  printf "  export RAZORPAY_KEY_ID=rzp_test_xxx RAZORPAY_KEY_SECRET=yyy && ./run.sh${Z}\n"
fi

printf "\n${B}Razorpay Intent Gateway${Z}  ${G}http://127.0.0.1:$PORT${Z}\n"
printf "${D}  Scripted scenarios tab -> six one-click cases\n"
printf "  Compose your own order  -> build a mandate and cart live\n"
printf "  Ctrl-C to stop${Z}\n\n"
( sleep 1; (command -v open >/dev/null && open "http://127.0.0.1:$PORT") \
  || (command -v xdg-open >/dev/null && xdg-open "http://127.0.0.1:$PORT") ) >/dev/null 2>&1 &
exec ./build/rig-gateway --port "$PORT"
