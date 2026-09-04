#!/usr/bin/env bash
# Drives the five-minute video, one beat per keypress. Nothing to type, nothing to
# paste, no markdown fences to run by accident.
#
#   ./scripts/record.sh          rehearsal: beat banners and spoken cues on screen
#   ./scripts/record.sh --clean  RECORDING: commands and output only, no cues
#   ./scripts/record.sh 3        jump to beat 3 (combine: --clean 3)
#   ./scripts/record.sh --list   show the beats and exit
#
# The beats here MUST mirror docs/03-DEMO-SCRIPT.md. They drifted once -- the driver
# still had an eight-beat running order while the script had nine, so the C++ kernel
# segment never ran on camera.
#
# SPACE / ENTER advances. q quits. Ctrl-C always works.
set -uo pipefail
cd "$(dirname "$0")/.."

B=$'\033[1m'; D=$'\033[2m'; Z=$'\033[0m'
C=$'\033[36m'; G=$'\033[32m'; Y=$'\033[33m'; M=$'\033[35m'

BEATS=(
  "0:00|The decision, measured"
  "0:28|The gap  (no command — talk only)"
  "0:55|Inside the kernel"
  "1:35|Durability"
  "2:20|Two languages, one verdict"
  "2:55|Try to break it"
  "3:30|Six industries, one number"
  "4:03|Graceful failure  (cut to the UI)"
  "4:28|The bug I found in myself"
)

if [ "${1:-}" = "--list" ]; then
  printf "\n${B}beats${Z}  ${D}mirrors docs/03-DEMO-SCRIPT.md${Z}\n"
  for i in "${!BEATS[@]}"; do
    printf "  ${C}%d${Z}  ${D}%-6s${Z} %s\n" $((i+1)) "${BEATS[$i]%%|*}" "${BEATS[$i]#*|}"
  done; printf "\n"; exit 0
fi

CLEAN=0
if [ "${1:-}" = "--clean" ]; then CLEAN=1; shift; fi
START=${1:-1}
case "$START" in ''|*[!0-9]*) START=1 ;; esac
[ "$START" -lt 1 ] && START=1

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

# Rehearsal-only reminders. Silent during a take, or the camera films a teleprompter.
note(){ [ "$CLEAN" = "1" ] || printf "${D}  → %s${Z}\n" "$1"; }
cue(){  [ "$CLEAN" = "1" ] || printf "\n${Y}  %s${Z}\n" "$1"; }

# Echo the command as if typed, then run it. The viewer must see the command.
run(){ printf "\n${G}\$${Z} ${B}%s${Z}\n\n" "$*"; eval "$@"; }

trap 'printf "\n${D}stopped.${Z}\n"; exit 0' INT

clear
if [ "$CLEAN" = "0" ]; then
  printf "${B}Razorpay Intent Gateway — recording driver${Z}\n"
  printf "${D}space advances · q quits · --clean hides these cues for the real take${Z}\n"
fi
./scripts/seed.sh >/dev/null 2>&1 || printf "${Y}seed failed — check ./verify.sh${Z}\n"
[ "$CLEAN" = "1" ] && clear
pause || exit 0

# ── 1 · 0:00 ────────────────────────────────────────────────────────────────
if [ "$START" -le 1 ]; then
  beat 0 "an authorisation decision on a shopping cart"
  pause || exit 0
  run ./build/bench-engine-kernel 3
  run ./build/bench-engine-kernel 8
  note "28 ns for 3 lines, 58 for 8 — it scales because it reads every line"
  note "the rail costs ~70 ms. this is a millionth of it. checking is free."
  pause || exit 0
fi

# ── 2 · 0:28 ────────────────────────────────────────────────────────────────
if [ "$START" -le 2 ]; then
  beat 1 "NPCI UAP, Agent Pay, AP2, ACP — none of them read the cart"
  cue "NO COMMAND. Talk over the benchmark still on screen."
  pause || exit 0
fi

# ── 3 · 0:55 ── the C++ segment ─────────────────────────────────────────────
if [ "$START" -le 3 ]; then
  beat 2 "branch-free, integer paise, never short-circuits"
  pause || exit 0
  run "sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40"
  note "24 reject codes, per-line attribution, 7 ns zero-init tax"
  note "a pure function with no I/O — which is what makes replay possible"
  pause || exit 0
fi

# ── 4 · 1:35 ────────────────────────────────────────────────────────────────
if [ "$START" -le 4 ]; then
  beat 3 "a decision is worthless unless it is durable before the money moves"
  cue "SPEAK FIRST — set up 'on macOS the disk lies' before the grep."
  pause || exit 0
  run "grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp"
  note "fsync returns success while the bytes sit in volatile cache"
  note "33 us for the lie, 4 ms for the truth — 250 decisions/s is not a gateway"
  pause || exit 0
  run ./build/rig-load 2000
  note "group commit: one F_FULLFSYNC for the whole batch"
  note "and that log is not only durability — it is the evidence"
  pause || exit 0
fi

# ── 5 · 2:20 ────────────────────────────────────────────────────────────────
if [ "$START" -le 5 ]; then
  beat 4 "two independent implementations agreeing is evidence"
  rm -f wal/rig.wal          # a retake must not collide with its own last take
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/lunch_cart.json --wal wal/rig.wal"
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/rig.wal"
  pause || exit 0
  run ./build/rig-audit wal/rig.wal
  run ./build/rig-replay wal/rig.wal
  note "the engine re-executes every decision from its recorded inputs"
  pause || exit 0
  run "java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal"
  note "484 lines of Java, no shared code, zero divergences"
  pause || exit 0
  run ./scripts/tamper.sh
  note "one flipped bit and the chain refuses to verify"
  pause || exit 0
fi

# ── 6 · 2:55 ────────────────────────────────────────────────────────────────
if [ "$START" -le 6 ]; then
  beat 5 "let the wall of REFUSED sit for two seconds before speaking"
  pause || exit 0
  run ./build/rig-attack
  note "8 refused, exactly 1 authorised. the agent holds no Razorpay credentials."
  pause || exit 0
fi

# ── 7 · 3:30 ────────────────────────────────────────────────────────────────
if [ "$START" -le 7 ]; then
  beat 6 "nothing changed but the catalogue"
  pause || exit 0
  run ./scripts/sectors.sh
  note "not one food concept in the kernel"
  pause || exit 0
  run ./build/rig-revenue
  note "a declined agent cart is a lost sale, not a saved rupee"
  note "98.3% unattended, held-out split, synthetic labelled as synthetic"
  pause || exit 0
fi

# ── 8 · 4:03 ────────────────────────────────────────────────────────────────
if [ "$START" -le 8 ]; then
  beat 7 "blocking without breaking what the user wanted is the product"
  rm -f wal/demo.wal
  pause || exit 0
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/demo.wal"
  note "three violations, the failing line named, and a repair block"
  cue "NOW CUT TO THE BROWSER — scenario 3 · Auto-repair — ~20 seconds"
  pause || exit 0
fi

# ── 9 · 4:28 ────────────────────────────────────────────────────────────────
if [ "$START" -le 9 ]; then
  beat 8 "quantity caps were per line, and the agent picks how many lines it sends"
  cue "SPEAK FIRST — tell the bug story, then run verify.sh under it."
  pause || exit 0
  run ./verify.sh
  note "42 checks, non-zero exit if any of it was a lie"
fi

[ "$CLEAN" = "1" ] || printf "\n\n${G}${B}  end of script.${Z}  ${D}retake: ./scripts/record.sh N${Z}\n\n"
