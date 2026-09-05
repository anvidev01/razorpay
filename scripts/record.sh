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
  "0:00|Cold open — the denial, already on screen"
  "0:18|Why now  (no command — talk only)"
  "1:00|Inside the kernel"
  "1:50|Durability"
  "2:22|Two languages, one verdict"
  "2:56|Try to break it"
  "3:19|Five industries, one number"
  "3:49|Live Razorpay read-back"
  "4:05|Third outcome — CUT TO BROWSER, no command"
  "4:14|The bug, and what to run"
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

# The frame should look like a shell someone is using, not a script printing commands.
# Rebuild the real zsh prompt: user@host dir %  -- override with RIG_PROMPT if your
# terminal is themed differently.
PROMPT_STR="${RIG_PROMPT:-$(whoami)@$(hostname -s) $(basename "$PWD") %}"

run(){
  printf "\n${G}%s${Z} " "$PROMPT_STR"
  type_out "$*"
  sleep 0.35                 # the beat between hitting Enter and output appearing
  printf "\n\n"
  eval "$@"
}

trap 'printf "\n${D}stopped.${Z}\n"; exit 0' INT

clear
if [ "$CLEAN" = "0" ]; then
  printf "${B}Mandate Engine — recording driver${Z}\n"
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

# ── 1 · 0:00 · cold open — run the denial, then speak over it ───────────────
if [ "$START" -le 1 ]; then
  beat 0 "a six-thousand-rupee blender out of a five-hundred-rupee lunch budget"
  rm -f wal/demo.wal
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/demo.wal"
  note "DO NOT narrate the command. Let 0x000D land, then speak."
  pause || exit 0
fi

# ── 2 · 0:18 · why now, no command ──────────────────────────────────────────
if [ "$START" -le 2 ]; then
  beat 1 "Razorpay+NPCI on Claude; NPCI's Unified Agent Protocol in four days"
  cue "NO COMMAND. Keep the denial on screen."
  pause || exit 0
fi

# ── 3 · 1:00 · inside the kernel (the C++ segment) ──────────────────────────
if [ "$START" -le 3 ]; then
  beat 2 "branch-free, integer paise, no model in this path"
  run "sed -n '/Verdict evaluate/,/^}/p' engine/src/kernel.cpp | head -40"
  note "24 reject codes, per-line attribution, 7 ns zero-init tax"
  note "evaluate() has NO utterance parameter"
  pause || exit 0
fi

# ── 4 · 1:50 · durability ───────────────────────────────────────────────────
if [ "$START" -le 4 ]; then
  beat 3 "durable before money moves; on macOS the disk lies"
  run "grep -A6 'F_FULLFSYNC' engine/include/rig/clock.hpp"
  note "fsync returns success while bytes sit in the drive's volatile cache"
  pause || exit 0
  run ./build/rig-load 2000
  note "~120 decisions per fsync: 47 us each, ~21,000/s"
  pause || exit 0
fi

# ── 5 · 2:22 · two languages, one verdict ───────────────────────────────────
if [ "$START" -le 5 ]; then
  beat 4 "mandate, cart, decision, THEN token — the ordering invariant"
  rm -f wal/rig.wal          # a retake must not collide with its own last take
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/lunch_cart.json --wal wal/rig.wal"
  run "./build/rig-eval fixtures/lunch_intent.json \\
    fixtures/blender_cart.json --wal wal/rig.wal"
  run ./build/rig-audit wal/rig.wal
  run ./build/rig-replay wal/rig.wal
  pause || exit 0
  run "java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal"
  note "no shared code, zero divergences"
  pause || exit 0
  run ./scripts/tamper.sh
  note "seven verified records become three"
  pause || exit 0
fi

# ── 6 · 2:56 · try to break it ──────────────────────────────────────────────
if [ "$START" -le 6 ]; then
  beat 5 "let the wall of REFUSED sit for two seconds before speaking"
  run ./build/rig-attack
  note "3 forgeries + 5 bypasses, all refused; 1 legitimate payment"
  pause || exit 0
fi

# ── 7 · 3:19 · five industries, one number ──────────────────────────────────
if [ "$START" -le 7 ]; then
  beat 6 "nothing changed but the product feed"
  run ./scripts/sectors.sh
  pause || exit 0
  run ./build/rig-revenue
  note "100% of authorised value completes, 98% unattended; blocker 94%"
  pause || exit 0
fi

# ── 8 · 3:49 · the live read-back — the moment a README cannot give ────────
if [ "$START" -le 8 ]; then
  beat 7 "that order id was created seconds ago; nothing local can fake it"
  run ./scripts/prove-razorpay.sh
  note "read back out of api.razorpay.com with mandate_id and wal_seq"
  pause || exit 0
fi

# ── 9 · 4:05 · third outcome — browser only, no terminal command ────────────
if [ "$START" -le 9 ]; then
  beat 8 "allow and deny you have seen; this is the third outcome"
  cue "CUT TO BROWSER. Click 5 · Hidden instructions, then Approve · MFA."
  pause || exit 0
fi

# ── 10 · 4:14 · the bug, and what to run ────────────────────────────────────
if [ "$START" -le 10 ]; then
  beat 9 "quantity caps were per line, and the agent picks how many lines"
  cue "SPEAK FIRST — tell the bug story, then run verify under the last line."
  pause || exit 0
  run ./verify.sh --quick
  note "36 checks in 3 seconds; the full run is 46"
fi

[ "$CLEAN" = "1" ] || printf "\n\n${G}${B}  end of script.${Z}  ${D}retake: ./scripts/record.sh N${Z}\n\n"
