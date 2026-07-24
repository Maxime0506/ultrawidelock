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

# The twin's firmware is modules/woz_uwb specifically — the DS-TWR responder,
# range store and facade. UNIT_SRCS is the whole host-coverage denominator and
# now carries sibling modules too (woz_aliro_stack, ...), which the twin never
# calls; -O2 would dead-strip them, but compiling them at all couples this
# build to modules the page has nothing to do with (a sibling that failed
# under emcc would break the twin for no reason). So take only the woz_uwb
# subset of UNIT_SRCS — self-maintaining as woz_uwb files come and go.
WOZ_UWB_SRCS=()
for src in "${UNIT_SRCS[@]}"; do
  case "$src" in
    */modules/woz_uwb/src/*) WOZ_UWB_SRCS+=("$src") ;;
  esac
done
if [ "${#WOZ_UWB_SRCS[@]}" -eq 0 ]; then
  echo "twin-wasm: no woz_uwb sources in UNIT_SRCS — sources.sh layout changed?" >&2
  exit 1
fi

# Same defines + include path as the host suite (run.sh); the woz_uwb sources,
# the radio-stub shim, the host AES double, and the shared peer model — plus
# the glue that exports the page's entry points.
emcc -std=c11 -O2 -w "${DEFS[@]}" "${INCS[@]}" \
  -ffile-prefix-map="$ROOT"=. \
  "${WOZ_UWB_SRCS[@]}" "${SHIM_SRCS[@]}" \
  "$ROOT/tests/host/aes_ref.c" \
  "$ROOT/tests/host/twin_frames.c" \
  "$ROOT/web-twin/twin_glue.c" \
  -sMODULARIZE=1 -sEXPORT_NAME=createTwin -sSINGLE_FILE=1 \
  -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
  -o "$ROOT/web-twin/twin.js"

echo "twin-wasm: built web-twin/twin.js ($(wc -c <"$ROOT/web-twin/twin.js" | tr -d ' ') bytes)"
