#!/usr/bin/env bash
# Resets the demo to a known state: fresh WAL, mandate valid from now.
# Deterministic -- run this immediately before recording.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
NOW_S=$(date +%s)
NB=$(( NOW_S - 60 ))000000000
NA=$(( NOW_S + 21600 ))000000000  # 6h window so the demo cannot silently expire mid-take
                                  # (TTL enforcement itself is proven by rig-tests: R_MANDATE_EXPIRED)
python3 - "$NB" "$NA" <<'PY'
import json, sys
nb, na = int(sys.argv[1]), int(sys.argv[2])
p = 'fixtures/lunch_intent.json'
d = json.load(open(p))
d['not_before_ns'], d['not_after_ns'] = nb, na
json.dump(d, open(p, 'w'), indent=2)
print(f"mandate window refreshed: {nb} .. {na}")
PY
rm -f wal/*.wal
mkdir -p wal
echo "wal cleared. demo is seeded."
