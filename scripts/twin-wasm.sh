#!/usr/bin/env bash
#
# Build the web twin's firmware: modules/woz_uwb + the tests/host shim compiled
# to WASM (Emscripten), driven by web-twin/twin_glue.c. Output is a single
# self-contained web-twin/twin.js (MODULARIZE + SINGLE_FILE: the .wasm rides
# embedded, so the page keeps working from file:// and the site copy stays a
# flat file pair). The compile is path-prefix-mapped for reproducibility: the
# same emsdk version must produce a byte-identical twin.js on any machine,
# which is what lets CI rebuild and diff it as a staleness gate.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tests/host/sources.sh"

# emcc: PATH first (CI installs it there); else the standard ~/emsdk install.
if ! command -v emcc >/dev/null 2>&1; then
  if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091  # user-local emsdk, not part of the repo
    . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
  fi
fi
if ! command -v emcc >/dev/null 2>&1; then
  echo "twin-wasm: emcc not found (install emsdk: https://emscripten.org/docs/getting_started/)" >&2
  exit 1
fi

# Same sources + defines as the host suite (run.sh), minus the test files: the
# unit list, the radio-stub shim, the host AES double, and the shared peer
# model — plus the glue that exports the page's entry points.
emcc -std=c11 -O2 -w "${DEFS[@]}" "${INCS[@]}" \
  -ffile-prefix-map="$ROOT"=. \
  "${UNIT_SRCS[@]}" "${SHIM_SRCS[@]}" \
  "$ROOT/tests/host/aes_ref.c" \
  "$ROOT/tests/host/twin_frames.c" \
  "$ROOT/web-twin/twin_glue.c" \
  -sMODULARIZE=1 -sEXPORT_NAME=createTwin -sSINGLE_FILE=1 \
  -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
  -o "$ROOT/web-twin/twin.js"

echo "twin-wasm: built web-twin/twin.js ($(wc -c <"$ROOT/web-twin/twin.js" | tr -d ' ') bytes)"
