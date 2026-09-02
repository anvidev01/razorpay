#!/usr/bin/env bash
# Every mandate carries a validity window, so every mandate fixture ages out.
# Committing a fixed not_after_ns means the repo passes its own verification on the
# day it is written and fails silently a few hours later -- which is exactly what
# happened to the five sector fixtures. Refresh all of them from one place.
#
# This does NOT weaken the TTL claim: expiry is enforced by the kernel and proven
# by rig-tests (R_MANDATE_EXPIRED) against a mandate that is genuinely in the past.
# Here we are only keeping the *demo* fixtures current.
set -euo pipefail
cd "$(dirname "$0")/.."

NOW_S=$(date +%s)
NB=$(( NOW_S - 60 ))000000000
NA=$(( NOW_S + 21600 ))000000000   # 6h: long enough that a recording cannot expire mid-take

python3 - "$NB" "$NA" <<'PY'
import glob, json, sys
nb, na = int(sys.argv[1]), int(sys.argv[2])
paths = sorted(set(glob.glob('fixtures/*_intent.json') +
                   glob.glob('fixtures/sectors/*_intent.json')))
n = 0
for p in paths:
    d = json.load(open(p))
    if 'not_after_ns' not in d:
        continue
    d['not_before_ns'], d['not_after_ns'] = nb, na
    json.dump(d, open(p, 'w'), indent=2)
    n += 1
print(f"{n} mandate windows refreshed: {nb} .. {na}")
PY
