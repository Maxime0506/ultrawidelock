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

# The twin's firmware is a deliberate, stable set: the modules/woz_uwb decision
# logic (DS-TWR responder, CCC crypto + codec, range store, facade). It is NOT
# the logging backends (woz_logfmt/woz_logquiet — the twin logs via
# printf -> Module.print) nor the STS seam layer (ccc_shim_wrap — the twin
# uses the host radio doubles in dw_rx_stub.c). An explicit allowlist, not a
# path filter over the shared UNIT_SRCS: that list is the host-COVERAGE
# denominator and grows target-only + coverage-only files (a coarse
# modules/woz_uwb/src/* filter swept woz_logfmt/woz_logquiet/ccc_shim_wrap in
# and drifted twin.js). A woz_uwb decision file the twin needs but omits here
# fails loud — the node selftest link-errors on the missing symbol. $SRC/$SHIM
# come from sources.sh; INCS/DEFS are reused as-is (extra -I dirs compile
# nothing, so their churn is harmless — only the source list matters).
WOZ_UWB_SRCS=(
  "$SRC/ccc/ccc_kdf.c" "$SRC/ccc/ccc_mac.c" "$SRC/ccc/ccc_session.c"
  "$SRC/ccc/ccc_shim.c" "$SRC/ccc/ccc_sts.c" "$SRC/ccc/cherry_ccc_shim.c"
  "$SRC/ccc/ccc_shim_rx.c"
  "$SRC/aliro/aliro_uwb_msg_builder.c" "$SRC/aliro/aliro_uwb_msg_parser.c"
  "$SRC/aliro/aliro_uwb_adapter.c" "$SRC/aliro/aliro_uwb_msg.c"
  "$SRC/aliro/aliro_uwb_session.c"
  "$SRC/fira/ds_twr.c" "$SRC/fira/fira_session.c"
  "$SRC/facade/woz_uwb_facade.c"
)
# Radio + STS-register doubles the responder links against (the host shim, not
# the target seam). Explicit for the same reason: SHIM_SRCS now carries
# logfake.c, which only the excluded woz_logfmt.c needs.
TWIN_SHIM_SRCS=("$SHIM/shim.c" "$SHIM/dw_rx_stub.c")

# Fail loud if any listed source was renamed/moved out from under us, rather
# than letting emcc emit a cryptic missing-input error.
for src in "${WOZ_UWB_SRCS[@]}" "${TWIN_SHIM_SRCS[@]}"; do
  if [ ! -f "$src" ]; then
    echo "twin-wasm: source not found: $src — did modules/woz_uwb or the shim move?" >&2
    exit 1
  fi
done

# The flight recorder is a device diagnostic: a RAM ring dumped over the serial
# console, which the twin has neither of. Its header stands in with no-op
# inlines when the define is absent, so dropping it here keeps both the recorder
# and the logging backend it prints through out of the twin's link — the host
# suite keeps building it with the define on.
TWIN_DEFS=()
for def in "${DEFS[@]}"; do
  case "$def" in
  -DCONFIG_WOZ_FLIGHT_RECORDER*) ;;
  *) TWIN_DEFS+=("$def") ;;
  esac
done

# The woz_uwb sources + the radio-stub shim, the host AES double and the shared
# peer model, plus the glue that exports the page's entry points. INCS/DEFS
# match the host suite (run.sh), less the recorder define above.
emcc -std=c11 -O2 -w "${TWIN_DEFS[@]}" "${INCS[@]}" \
  -ffile-prefix-map="$ROOT"=. \
  "${WOZ_UWB_SRCS[@]}" "${TWIN_SHIM_SRCS[@]}" \
  "$ROOT/tests/host/aes_ref.c" \
  "$ROOT/tests/host/twin_frames.c" \
  "$ROOT/web-twin/twin_glue.c" \
  -sMODULARIZE=1 -sEXPORT_NAME=createTwin -sSINGLE_FILE=1 \
  -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
  -o "$ROOT/web-twin/twin.js"

echo "twin-wasm: built web-twin/twin.js ($(wc -c <"$ROOT/web-twin/twin.js" | tr -d ' ') bytes)"
