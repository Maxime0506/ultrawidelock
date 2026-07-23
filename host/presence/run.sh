#!/usr/bin/env bash
#
# Build + run the libaliro_presence host unit tests (config, framing, verify, TTL
# cache, socketpair transact). Plain C, no PAM/hardware. SAN=1 rebuilds under
# ASan + UBSan. Mirrors tests/host/run.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HERE="$ROOT/host/presence"
ALIRO="$ROOT/modules/woz_aliro"

mkdir -p "$ROOT/build/presence"

san_flags=
if [ -n "${SAN:-}" ]; then
	san_flags='-g -fsanitize=address,undefined -fno-sanitize-recover=all'
fi

INCS=(-I"$HERE" -I"$ALIRO/include" -I"$ALIRO/src")
DEPS=("$HERE/aliro_presence.c" "$ALIRO/src/aliro_assert.c" "$ALIRO/src/aliro_hash.c")

# shellcheck disable=SC2086  # san_flags is a deliberate word-split flag list
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra $san_flags "${INCS[@]}" \
	"$HERE/test_presence.c" "${DEPS[@]}" -o "$ROOT/build/presence/test_presence"

"$ROOT/build/presence/test_presence"
