#!/usr/bin/env bash
# No Maven, no Gradle -- plain javac. Fewer moving parts three days from a deadline.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
javac -d out $(find src -name '*.java')
echo "built -> control-plane/out"
