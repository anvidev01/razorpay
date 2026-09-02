#!/usr/bin/env bash
# Resets the demo to a known state: fresh WAL, mandate valid from now.
# Deterministic -- run this immediately before recording.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
./scripts/refresh-windows.sh

rm -f wal/*.wal
mkdir -p wal
echo "wal cleared. demo is seeded."
