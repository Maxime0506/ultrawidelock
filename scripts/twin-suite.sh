#!/usr/bin/env bash
#
# The web-twin suite for the umbrella runner (make check): the constant-drift
# gate (always) plus the WASM twin's node self-test against the committed
# web-twin/twin.js (when node is present). No rebuild here — regenerating
# twin.js needs a pinned emsdk and is CI's byte-diff staleness gate; this only
# proves the committed firmware artifact still passes its scenario.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

python3 "$ROOT/web-twin/check_constants.py"

if command -v node >/dev/null 2>&1; then
	node "$ROOT/web-twin/selftest.cjs"
else
	echo "  web-twin selftest skipped (node not found)"
fi
