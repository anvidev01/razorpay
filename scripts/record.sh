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
  "0:00|The problem, one line  (no command — talk only)"
  "0:10|The decision, measured"
  "0:38|The gap  (no command — talk only)"
  "1:00|Inside the kernel"
  "1:35|Durability"
  "2:20|Two languages, one verdict"
  "2:55|Try to break it"
  "3:30|Five industries, one number"
  "4:03|Graceful failure + retry storm  (cut to the UI)"
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

# During a take the terminal is the frame, so the prompt itself must not appear --
# it sat on screen for the whole beat while the operator was speaking. In --clean the
# wait is completely silent; only rehearsal shows the hint.
pause(){
  local k
  if [ "$CLEAN" = "1" ]; then
    IFS= read -rsn1 k </dev/tty || true
  else
    printf "\n${D}   [space] next   [q] quit${Z} "
    IFS= read -rsn1 k </dev/tty || true
    printf "\r\033[K"
  fi
  [ "$k" = "q" ] && return 1 || return 0
}

beat(){                       # $1 = index, $2 = spoken cue
  local i=$1
  if [ "$CLEAN" = "1" ]; then printf "\n"; return 0; fi
  printf "\n\n${C}════════════════════════════════════════════════════════════════${Z}\n"
  printf "  ${B}%s${Z}  ${M}%s${Z}\n" "${BEATS[$i]%%|*}" "${BEATS[$i]#*|}"
  printf "${C}════════════════════════════════════════════════════════════════${Z}\n"
  [ -n "${2:-}" ] && printf "${D}  say: %s${Z}\n" "$2"
}

# Rehearsal-only reminders. Silent during a take, or the camera films a teleprompter.
note(){ [ "$CLEAN" = "1" ] || printf "${D}  → %s${Z}\n" "$1"; }
cue(){  [ "$CLEAN" = "1" ] || printf "\n${Y}  %s${Z}\n" "$1"; }

# Type the command out, then run it.
#
# Printing the command and its output in the same instant reads as a paste, not as
# someone working -- the viewer never sees the command as a separate act. So the
# characters land one at a time, then there is a beat (as if pressing Enter) before
# any output appears.
#
# Typing the command out character by character costs about 23 seconds across the
# sixteen commands in this script -- 8% of a five-minute video, spent on an animation
# rather than on substance. So it is OFF by default. What IS kept is a short beat
# between the command and its output, which is what actually stops the two reading as
# one paste, and costs about five seconds in total.
#
#   RIG_TYPE_MS=16  type it out, if you decide you prefer the look
TYPE_MS="${RIG_TYPE_MS:-0}"

type_out(){
  local str="$1" i c
  if [ "$TYPE_MS" = "0" ]; then printf "${B}%s${Z}" "$str"; return; fi
  local delay
  delay=$(awk -v m="$TYPE_MS" 'BEGIN{printf "%.3f", m/1000}')
  printf "${B}"
  for (( i=0; i<${#str}; i++ )); do
    c="${str:$i:1}"
    printf '%s' "$c"
    # a newline in a wrapped command is a natural place to breathe
    if [ "$c" = $'\n' ]; then sleep 0.12; else sleep "$delay"; fi
  done
  printf "${Z}"
}

run(){
  printf "\n${G}\$${Z} "
  type_out "$*"
  sleep 0.35                 # the beat between hitting Enter and output appearing
  printf "\n\n"
  eval "$@"
}

trap 'printf "\n${D}stopped.${Z}\n"; exit 0' INT

clear
if [ "$CLEAN" = "0" ]; then
  printf "${B}Razorpay Intent Gateway — recording driver${Z}\n"
  printf "${D}space advances · q quits · --clean hides these cues for the real take${Z}\n"
fi
./scripts/seed.sh >/dev/null 2>&1 || printf "${Y}seed failed — check ./verify.sh${Z}\n"
[ "$CLEAN" = "1" ] && clear

# Ten beats. Indices and guards are sequential and MUST stay that way -- they drifted
# once, which left two beats sharing an index and the gap beat ordered after the
# benchmark it is supposed to follow.
#
# One press of space advances to the next beat AND runs its first command. Pauses sit at
# the END of a beat, never after a banner, so nobody presses space just to reveal a
# header.

# ── 1 · 0:00 · the problem, no command ──────────────────────────────────────
if [ "$START" -le 1 ]; then
  beat 0 "an agent can spend your money on something you never agreed to buy"
  cue "NO COMMAND. Empty prompt on screen. Say the problem, then press space."
  pause || exit 0
fi

# ── 2 · 0:10 · the decision, measured ───────────────────────────────────────
if [ "$START" -le 2 ]; then
  beat 1 "an authorisation decision on a shopping cart"
  run ./build/bench-engine-kernel 3
  run ./build/bench-engine-kernel 8
  note "28 ns for 3 lines, 58 for 8 — it scales because it reads every line"
  note "the rail costs ~70 ms. this is a millionth of it. checking is free."
  pause || exit 0
fi

# ── 3 · 0:38 · the gap, no command ──────────────────────────────────────────
if [ "$START" -le 3 ]; then
  beat 2 "NPCI UAP, Agent Pay, AP2, ACP — none of them read the cart"
  cue "NO COMMAND. Talk over the benchmark still on screen."
  pause || exit 0
fi

# ── 4 · 1:00 · inside the kernel (the C++ segment) ──────────────────────────
if [ "$START" -le 4 ]; then
  beat 3 "branch-free, integer paise, never short-circuits"
  run "sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40"
  note "24 reject codes, per-line attribution, 7 ns zero-init tax"
  note "no model in this path — a pure function, which is what makes replay possible"
  pause || exit 0
fi

# ── 5 · 1:35 · durability ───────────────────────────────────────────────────
if [ "$START" -le 5 ]; then
  beat 4 "a decision is worthless unless it is durable before the money moves"
  cue "SPEAK FIRST — set up 'on macOS the disk lies' before the grep."
  run "grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp"
  note "fsync returns success while the bytes sit in the drive's volatile cache"
  note "33 us for the lie, 4 ms for the truth — 250 decisions/s is not a gateway"
  pause || exit 0
  run ./build/rig-load 2000
  note "group commit: one F_FULLFSYNC for the whole batch"
  note "and that log is not only durability — it is the evidence"
  pause || exit 0
fi

# ── 6 · 2:20 · two languages, one verdict ───────────────────────────────────
if [ "$START" -le 6 ]; then
  beat 5 "two independent implementations agreeing is evidence"
  rm -f wal/rig.wal          # a retake must not collide with its own last take
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

# ── 7 · 2:55 · try to break it ──────────────────────────────────────────────
if [ "$START" -le 7 ]; then
  beat 6 "let the wall of REFUSED sit for two seconds before speaking"
  run ./build/rig-attack
  note "8 refused, exactly 1 authorised. the agent holds no Razorpay credentials."
  pause || exit 0
fi

# ── 8 · 3:30 · five industries, one number ──────────────────────────────────
if [ "$START" -le 8 ]; then
  beat 7 "nothing changed but the catalogue"
  run ./scripts/sectors.sh
  note "five sectors, five controls, one binary"
  pause || exit 0
  run ./build/rig-revenue
  note "a declined agent cart is a lost sale, not a saved rupee"
  note "nets Rs 50,000 ahead; 98% unattended on a held-out split"
  pause || exit 0
fi

# ── 9 · 4:03 · graceful failure + retry storm ───────────────────────────────
if [ "$START" -le 9 ]; then
  beat 8 "blocking without breaking what the user wanted is the product"
  rm -f wal/demo.wal
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/demo.wal"
  note "three violations, the failing line named, no capability token"
  pause || exit 0
  # Retry storm: the SAME basket re-generated. First ALLOW, second collapses.
  run "./build/rig-eval fixtures/grocery_intent.json \\
    fixtures/cart_retry_a.json --wal wal/demo.wal"
  run "./build/rig-eval fixtures/grocery_intent.json \\
    fixtures/cart_retry_b.json --wal wal/demo.wal"
  note "semantic idempotency collapsed the retry — no second charge"
  cue "NOW CUT TO THE BROWSER — scenario 3 · Auto-repair — ~20 seconds"
  pause || exit 0
fi

# ── 10 · 4:28 · the bug I found in myself ───────────────────────────────────
if [ "$START" -le 10 ]; then
  beat 9 "quantity caps were per line, and the agent picks how many lines it sends"
  cue "SPEAK FIRST — tell the bug story, then run verify.sh under it."
  run ./verify.sh
  note "42 checks, non-zero exit if any of it was a lie"
fi

[ "$CLEAN" = "1" ] || printf "\n\n${G}${B}  end of script.${Z}  ${D}retake: ./scripts/record.sh N${Z}\n\n"
