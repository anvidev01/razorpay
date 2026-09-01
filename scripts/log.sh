#!/usr/bin/env bash
# Append a timestamped entry to BUILD_LOG.md and commit it under the repo author.
# Usage: ./scripts/log.sh "<phase>" "<what changed>" [--commit]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
PHASE="${1:?phase required}"
NOTE="${2:?note required}"
TS="$(date -u '+%Y-%m-%d %H:%M:%SZ')"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo 'initial')"
printf '\n### %s — %s\n- **at:** `%s` (HEAD `%s`)\n- %s\n' "$TS" "$PHASE" "$TS" "$SHA" "$NOTE" >> BUILD_LOG.md
echo "logged: $PHASE"
if [[ "${3:-}" == "--commit" ]]; then
  git add -A
  git commit -q -m "$PHASE: $NOTE"
  echo "committed as $(git config user.name) <$(git config user.email)>"
fi
