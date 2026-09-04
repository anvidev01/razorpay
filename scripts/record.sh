#!/usr/bin/env bash
# Drives the 5-minute video, one beat per keypress. Nothing to copy, nothing to
# mistype, no markdown fences to paste by accident -- the two failure modes that
# have already cost a take.
#
#   ./scripts/record.sh          rehearsal: beat banners and spoken cues on screen
#   ./scripts/record.sh --clean  RECORDING: commands and output only, no cues
#   ./scripts/record.sh 4        jump to beat 4 (combine: --clean 4)
#   ./scripts/record.sh --list   show the beats and exit
#
# Use --clean for the real take. The cues are for rehearsal; on camera they read as a
# teleprompter and undercut the demo.
#
# SPACE / ENTER advances. q quits. Ctrl-C always works.
set -uo pipefail
cd "$(dirname "$0")/.."

B=$'\033[1m'; D=$'\033[2m'; Z=$'\033[0m'
C=$'\033[36m'; G=$'\033[32m'; Y=$'\033[33m'; M=$'\033[35m'

BEATS=(
  "0:00|Cold open — the decision, measured"
  "0:35|The gap it closes  (no command — talk only)"
  "0:55|Durability — why fsync lies on macOS"
  "1:55|Dual-audit across two languages"
  "2:45|Graceful failure and auto-repair"
  "3:35|Try to get around it"
  "4:05|One engine, five industries"
  "4:25|Close — 40 checks"
)

if [ "${1:-}" = "--list" ]; then
  printf "\n${B}beats${Z}\n"
  for i in "${!BEATS[@]}"; do
    printf "  ${C}%d${Z}  ${D}%-6s${Z} %s\n" $((i+1)) "${BEATS[$i]%%|*}" "${BEATS[$i]#*|}"
  done; printf "\n"; exit 0
fi

CLEAN=0
if [ "${1:-}" = "--clean" ]; then CLEAN=1; shift; fi

START=${1:-1}
case "$START" in ''|*[!0-9]*) START=1 ;; esac
[ "$START" -lt 1 ] && START=1

# Wait for a keypress. Returns 1 if the operator asked to quit.
pause(){
  printf "\n${D}   [space] next   [q] quit${Z} "
  local k; IFS= read -rsn1 k </dev/tty || true
  printf "\r\033[K"
  [ "$k" = "q" ] && return 1 || return 0
}

beat(){                       # $1 = index, $2 = spoken cue
  local i=$1
  if [ "$CLEAN" = "1" ]; then printf "\n\n"; return 0; fi
  printf "\n\n${C}════════════════════════════════════════════════════════════════${Z}\n"
  printf "  ${B}%s${Z}  ${M}%s${Z}\n" "${BEATS[$i]%%|*}" "${BEATS[$i]#*|}"
  printf "${C}════════════════════════════════════════════════════════════════${Z}\n"
  [ -n "${2:-}" ] && printf "${D}  say: %s${Z}\n" "$2"
}

# Echo the command the way it would look typed, then run it. The viewer needs to
# see the command, so this is not cosmetic.
run(){
  printf "\n${G}\$${Z} ${B}%s${Z}\n\n" "$*"
  eval "$@"
}

# Reminder lines: rehearsal only, silent during a take.
note(){
  local nl=""; [ "$1" = "--nl" ] && { nl=$'\n'; shift; }
  [ "$CLEAN" = "1" ] && return 0
  printf "%s${D}  → %s${Z}\n" "$nl" "$1"
}

trap 'printf "\n${D}stopped.${Z}\n"; exit 0' INT

clear
if [ "$CLEAN" = "0" ]; then
  printf "${B}Razorpay Intent Gateway — recording driver${Z}\n"
  printf "${D}space advances · q quits · --clean hides these cues for the real take${Z}\n"
fi

# A stale WAL is the difference between ALLOW and R_DUPLICATE_CHARGE on camera.
./scripts/seed.sh >/dev/null 2>&1 || printf "${Y}seed failed — check ./verify.sh${Z}\n"
[ "$CLEAN" = "1" ] && clear
pause || exit 0

# ── 1 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 1 ]; then
  beat 0 "an authorisation decision on a shopping cart"
  pause || exit 0
  run ./build/bench-engine-kernel 3
  run ./build/bench-engine-kernel 8
  note --nl "it scales with the cart, because it reads every line."
  note "the rail it protects costs 69 ms. this is a millionth of that."
  pause || exit 0
fi

# ── 2 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 2 ]; then
  beat 1 "NPCI, Agent Pay, AP2, ACP — none of them read the cart"
  printf "\n${D}  No command. Talk over the previous screen.${Z}\n"
  pause || exit 0
fi

# ── 3 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 3 ]; then
  beat 2 "a decision is worthless unless it is durable before the money moves"
  pause || exit 0
  run "grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp"
  note --nl "fsync() on macOS returns success while the bytes sit in drive cache."
  pause || exit 0
  printf "\n${B}  the honest fence is 118x more expensive${Z}\n"
  printf "${D}  WAL fsync                    33.5 us   <- does not flush drive cache${Z}\n"
  printf "${D}  WAL fcntl(F_FULLFSYNC)    3 960 us   <- true durability${Z}\n"
  pause || exit 0
  run ./build/rig-load 2000
  note --nl "group commit: one F_FULLFSYNC for the whole batch."
  note "same fence, ~600x the throughput, nobody waits over 2 ms."
  pause || exit 0
fi

# ── 4 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 4 ]; then
  beat 3 "two independent implementations agreeing is evidence"
  rm -f wal/rig.wal          # a retake must not collide with its own last take
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/lunch_cart.json --wal wal/rig.wal"
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/rig.wal"
  pause || exit 0
  run ./build/rig-audit wal/rig.wal
  pause || exit 0
  run ./build/rig-replay wal/rig.wal
  note --nl "the engine re-executes every decision from recorded inputs."
  pause || exit 0
  run "java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal"
  note --nl "a Java re-implementation that shares no code. zero divergences."
  pause || exit 0
  run ./scripts/tamper.sh
  note --nl "one flipped bit and the chain refuses to verify."
  pause || exit 0
fi

# ── 5 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 5 ]; then
  beat 4 "blocking is easy; blocking without breaking the user's goal is the product"
  rm -f wal/demo.wal
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/demo.wal"
  note --nl "three violations at once; the kernel never short-circuits."
  note "per-line attribution: line three is the problem."
  note "'repair' says what would make this pass."
  note "no capability token: there is nothing to pay with."
  [ "$CLEAN" = "1" ] || printf "\n${Y}  NOW CUT TO THE UI — scenario 3 · Auto-repair — ~20 s${Z}\n"
  pause || exit 0
fi

# ── 6 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 6 ]; then
  beat 5 "let the wall of REFUSED sit for two seconds before speaking"
  pause || exit 0
  run ./build/rig-attack
  note --nl "the agent holds no Razorpay credentials at any point."
  pause || exit 0
fi

# ── 7 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 7 ]; then
  beat 6 "nothing changed but the fixtures"
  pause || exit 0
  run ./scripts/sectors.sh
  note --nl "not one food concept in the kernel. this is payments infrastructure."
  pause || exit 0
fi

# ── 8 ────────────────────────────────────────────────────────────────────────
if [ "$START" -le 8 ]; then
  beat 7 "quantity caps were per line, and the agent chooses how many lines it sends"
  printf "\n${D}  Tell the bug story here, then run the last command.${Z}\n"
  pause || exit 0
  run ./verify.sh
  note --nl "forty checks, and it exits non-zero if any of this was a lie."
fi

[ "$CLEAN" = "1" ] || printf "\n\n${G}${B}  end of script.${Z}  ${D}retake: ./scripts/record.sh N${Z}\n\n"
