#!/usr/bin/env bash
# Reproduces every number in docs/BENCHMARKS.md on this machine.
set -uo pipefail
cd "$(dirname "$0")"
SJ=$(brew --prefix simdjson 2>/dev/null || echo /opt/homebrew/opt/simdjson)
SSL=$(brew --prefix openssl@3 2>/dev/null || echo /opt/homebrew/opt/openssl@3)
CXX="c++ -std=c++20 -O3 -march=native"
mkdir -p ../../bench-results
run(){ echo; echo "=== $1 ==="; shift; "$@"; }

$CXX bench_timer_resolution.cpp -o /tmp/b_timer && run "timer resolution"      /tmp/b_timer
$CXX bench_kernel.cpp           -o /tmp/b_kern  && run "policy kernel scaling" bash -c 'for n in 1 4 8 16 32; do /tmp/b_kern $n; done'
$CXX bench_scan_handrolled.cpp  -o /tmp/b_hand  && run "hand-rolled scanner"   /tmp/b_hand
$CXX -I$SJ/include bench_scan_simdjson.cpp -L$SJ/lib -lsimdjson -o /tmp/b_sj && run "simdjson ondemand" /tmp/b_sj
$CXX -I$SSL/include bench_ed25519.cpp -L$SSL/lib -lcrypto -o /tmp/b_ed && run "ed25519 vs hmac" /tmp/b_ed
$CXX bench_false_sharing.cpp    -o /tmp/b_fs    && run "false sharing granule" /tmp/b_fs
$CXX bench_wal_durability.cpp   -o /tmp/b_fsy   && run "WAL durability"        /tmp/b_fsy
echo; echo "All benchmarks complete."
