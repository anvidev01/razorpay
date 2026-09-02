#!/usr/bin/env bash
# Fills a mandate's validity window with a real one, since epoch-nanosecond
# timestamps are not something anyone should hand-write.
#   ./scripts/mandate.sh <intent.json> [minutes]   (default 30)
set -euo pipefail
f="${1:?usage: ./scripts/mandate.sh <intent.json> [minutes]}"
mins="${2:-30}"
python3 - "$f" "$mins" <<'PY'
import json, sys, time
path, mins = sys.argv[1], int(sys.argv[2])
d = json.load(open(path))
now = time.time()
d["not_before_ns"] = int((now - 60) * 1e9)          # 60s of clock slack
d["not_after_ns"]  = int((now + mins * 60) * 1e9)
json.dump(d, open(path, "w"), indent=2)
print(f"{path}: valid for {mins} min from now")
PY
