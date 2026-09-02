#!/usr/bin/env bash
# Proves the audit log is tamper-EVIDENT: edit one byte, the chain refuses to verify.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
WAL="${1:-wal/rig.wal}"
[[ -f "$WAL" ]] || { echo "no WAL at $WAL -- run scripts/demo.sh first"; exit 1; }

echo
echo "  before tampering:"
./build/rig-replay "$WAL" | sed -n '3,4p'

cp "$WAL" "$WAL.bak"
OFF=$(python3 -c "
import os
n=os.path.getsize('$WAL')
print(n//2)")
python3 - "$WAL" "$OFF" <<'PY'
import sys
p, off = sys.argv[1], int(sys.argv[2])
d = bytearray(open(p,'rb').read())
old = d[off]
d[off] ^= 0x01
open(p,'wb').write(d)
print(f"  flipped one bit at byte {off}: 0x{old:02x} -> 0x{d[off]:02x}")
PY

echo
echo "  after tampering:"
./build/rig-replay "$WAL" | sed -n '3,5p' || true
mv "$WAL.bak" "$WAL"
echo
echo "  (original log restored)"
