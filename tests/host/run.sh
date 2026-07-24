#!/usr/bin/env bash
#
# Build + run the full host test suite (correctness gate). Plain C, no NCS
# toolchain or hardware. Compiles our logic modules against tests/host/shim and
# runs every module suite. `make coverage` builds the same sources instrumented.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

mkdir -p "$ROOT/build"
# SAN=1: same suite rebuilt under ASan + UBSan (`make test-san`).
san_flags=
if [ -n "${SAN:-}" ]; then
  san_flags='-g -fsanitize=address,undefined -fno-sanitize-recover=all'
fi
# -w: the shim intentionally leaves some args unused, and the in-tree modules are
# lint-gated by the real Zephyr build, not here. Errors still fail the build.
# shellcheck disable=SC2086  # san_flags is a deliberate word-split flag list
"${CC:-cc}" -std=c11 -O1 -w $san_flags "${DEFS[@]}" "${INCS[@]}" \
   "${TEST_SRCS[@]}" "${SHIM_SRCS[@]}" "${UNIT_SRCS[@]}" \
   -o "$ROOT/build/host_test"
# Quiet: suites assert, they don't need the UWB diag firehose on stdout (run
# the binary directly, without WOZ_TEST_QUIET, to get it back).
WOZ_TEST_QUIET=1 "$ROOT/build/host_test"

# --- target-only sources, separate small binaries --------------------------
# These compile production sources whose exported symbols the main binary
# already fakes (dw_rx_stub.c) or that need incompatible fakes, so they cannot
# join host_test. All of them run against recording doubles: branch logic and
# argument plumbing only, no hardware or crypto truth.
SRC="$ROOT/modules/woz_uwb/src"
HOSTD="$ROOT/tests/host"

# 1) uwb driver + shell (uwb_min/isr/rxdiag/selftest + aliro_shell) on the
#    drvfake radio + logfake zephyr surface.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags \
	-DWOZ_PORT_HOST -D_DEFAULT_SOURCE -DCONFIG_WOZ_ALIRO=1 \
	-DCONFIG_WOZ_UWB_SELFTEST_DELAY_MS=250 \
	-I"$HOSTD/shim" -I"$HOSTD" -I"$HOSTD/logfake" \
	-I"$SRC/driver" -I"$SRC/ccc" -I"$SRC/fira" -I"$SRC/facade" \
	-I"$ROOT/modules/woz_port/include" -I"$ROOT/deps/dw3000/platform" \
	"$HOSTD/test.c" "$HOSTD/drv_main.c" \
	"$HOSTD/test_uwb_min.c" "$HOSTD/test_uwb_isr.c" "$HOSTD/test_uwb_rxdiag.c" \
	"$HOSTD/test_uwb_selftest.c" "$HOSTD/test_aliro_shell.c" \
	"$HOSTD/shim/drvfake.c" \
	"$SRC/driver/uwb_min.c" "$SRC/driver/uwb_isr.c" "$SRC/driver/uwb_rxdiag.c" \
	"$SRC/driver/uwb_selftest.c" "$SRC/shell/aliro_shell.c" \
	-o "$ROOT/build/host_test_drv"
WOZ_TEST_QUIET=1 "$ROOT/build/host_test_drv"

# 2) PSA/mbedTLS crypto seams over recording fakes (psafake/). The two backend
#    files define the same crypto_aes_ecb_encrypt symbol as aes_ref.c, so each
#    is compiled alone with a -D rename (a compile flag, not a source edit).
psa_flags=(-std=c11 -O1 -w -I"$HOSTD/psafake" -I"$SRC/ccc")
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags -c \
	-Dcrypto_aes_ecb_encrypt=woz_test_psa_ecb \
	"$SRC/ccc/ccc_crypto_psa.c" -o "$ROOT/build/ccc_crypto_psa_host.o"
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags -c \
	-Dcrypto_aes_ecb_encrypt=woz_test_mbedtls_ecb \
	"$SRC/ccc/ccc_crypto_mbedtls.c" -o "$ROOT/build/ccc_crypto_mbedtls_host.o"
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags \
	-I"$HOSTD" -I"$ROOT/modules/woz_aliro/include" \
	"$HOSTD/test.c" "$HOSTD/test_psa_backends.c" "$HOSTD/psafake/psafake.c" \
	"$ROOT/modules/woz_aliro/src/aliro_prim_psa.c" \
	"$ROOT/build/ccc_crypto_psa_host.o" "$ROOT/build/ccc_crypto_mbedtls_host.o" \
	-o "$ROOT/build/host_test_psa"
"$ROOT/build/host_test_psa"

# 3) NFC ECP emitter (C++) over fake RFAL/reader-storage headers (ecpfake/).
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -c "$HOSTD/test.c" -o "$ROOT/build/test_harness_c.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags \
	-DCONFIG_DOOR_LOCK_RFAL_LOG_LEVEL=3 \
	-I"$HOSTD" -I"$HOSTD/ecpfake" \
	"$HOSTD/test_nfc_ecp.cpp" "$ROOT/modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp" \
	"$ROOT/build/test_harness_c.o" \
	-o "$ROOT/build/host_test_ecp"
"$ROOT/build/host_test_ecp"

# Host-side tooling tests (pure-stdlib Python; no toolchain involved).
# test_flash_html needs the python-markdown package and skips cleanly without.
# Each suite is folded to one summary row matching the side binaries above;
# the full unittest log is replayed on failure.
py_suite() { # <name> <script> <note>
	local out ran skipped note
	if ! out="$(python3 "$2" 2>&1)"; then
		printf '%s\n' "$out"
		printf '  %s: FAIL\n' "$1"
		exit 1
	fi
	ran="$(printf '%s' "$out" | sed -n 's/^Ran \([0-9]*\) tests*.*/\1/p')"
	skipped="$(printf '%s' "$out" | sed -n 's/.*skipped=\([0-9]*\).*/\1/p')"
	skipped="${skipped:-0}"
	note="$3"
	[ "$skipped" -gt 0 ] && note="$note, $skipped skipped"
	printf '  %s: PASS (%d checks — %s)\n' "$1" "$((ran - skipped))" "$note"
}
py_suite aliro-lab "$ROOT/tests/host/test_aliro_lab.py" "python, log-report tooling"
py_suite mqtt-bridge "$ROOT/tests/host/test_mqtt_bridge.py" "python, fake paho/serial"
py_suite flash-html "$ROOT/tests/host/test_flash_html.py" "python, needs markdown pkg"
