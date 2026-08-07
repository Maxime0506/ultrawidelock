#!/usr/bin/env bash
#
# Build + run the full host test suite (correctness gate). Plain C, no NCS
# toolchain or hardware. Compiles our logic modules against tests/host/shim and
# runs every module suite. `make coverage` builds the same sources instrumented.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/tests/host/sources.sh"

# One build root for the whole repo; the host suites own build/host.
OUT="${ALIRO_BUILD_ROOT:-$ROOT/build}/host"
mkdir -p "$OUT"
# SAN=1: same suite rebuilt under ASan + UBSan (`make test-san`).
san_flags=
if [ -n "${SAN:-}" ]; then
  san_flags='-g -fsanitize=address,undefined -fno-sanitize-recover=all'
fi
# -w: the shim intentionally leaves some args unused, and the in-tree modules are
# lint-gated by the real Zephyr build, not here. Errors still fail the build.
# shellcheck disable=SC2086  # san_flags is a deliberate word-split flag list
# -lm: test_woz_door.c derives its expected chord lengths with cos/sqrt rather
# than restating the module's arithmetic. glibc keeps those in libm, so the link
# fails on Linux without this; on macOS libSystem already has them and the flag
# is a no-op, which is why this only ever breaks in CI.
"${CC:-cc}" -std=c11 -O1 -w $san_flags "${DEFS[@]}" "${INCS[@]}" \
   "${TEST_SRCS[@]}" "${SHIM_SRCS[@]}" "${UNIT_SRCS[@]}" \
   -lm -o "$OUT/host_test"
# Quiet: suites assert, they don't need the UWB diag firehose on stdout (run
# the binary directly, without WOZ_TEST_QUIET, to get it back).
WOZ_TEST_QUIET=1 "$OUT/host_test"

# --- target-only sources, separate small binaries --------------------------
# These compile production sources whose exported symbols the main binary
# already fakes (dw_rx_stub.c) or that need incompatible fakes, so they cannot
# join host_test. All of them run against recording doubles: branch logic and
# argument plumbing only, no hardware or crypto truth.
SRC="$ROOT/modules/woz_uwb/src"
HOSTD="$ROOT/tests/host"

# 1) uwb driver + shell (uwb_min/isr/rxdiag/cirdiag/selftest + aliro_shell) on
#    the drvfake radio + logfake zephyr surface.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags \
	-DWOZ_PORT_HOST -D_DEFAULT_SOURCE -DCONFIG_WOZ_ALIRO=1 -DCONFIG_WOZ_UWB_CIRDIAG=1 \
	-DCONFIG_WOZ_UWB_SELFTEST_DELAY_MS=250 \
	-I"$HOSTD/shim" -I"$HOSTD" -I"$HOSTD/logfake" \
	-I"$SRC/driver" -I"$SRC/ccc" -I"$SRC/fira" -I"$SRC/facade" -I"$SRC/shell" \
	-I"$ROOT/modules/woz_port/include" -I"$ROOT/deps/dw3000/platform" \
	"$HOSTD/test.c" "$HOSTD/drv_main.c" \
	"$HOSTD/test_uwb_min.c" "$HOSTD/test_uwb_isr.c" "$HOSTD/test_uwb_rxdiag.c" \
	"$HOSTD/test_uwb_cirdiag.c" \
	"$HOSTD/test_uwb_selftest.c" "$HOSTD/test_aliro_shell.c" \
	"$HOSTD/shim/drvfake.c" \
	"$SRC/driver/uwb_min.c" "$SRC/driver/uwb_isr.c" "$SRC/driver/uwb_rxdiag.c" \
	"$SRC/driver/uwb_cirdiag.c" \
	"$SRC/driver/uwb_selftest.c" "$SRC/shell/aliro_shell.c" \
	-o "$OUT/host_test_drv"
WOZ_TEST_QUIET=1 "$OUT/host_test_drv"

# 2) PSA/mbedTLS crypto seams over recording fakes (psafake/). The two backend
#    files define the same crypto_aes_ecb_encrypt symbol as aes_ref.c, so each
#    is compiled alone with a -D rename (a compile flag, not a source edit).
psa_flags=(-std=c11 -O1 -w -I"$HOSTD/psafake" -I"$SRC/ccc")
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags -c \
	-Dcrypto_aes_ecb_encrypt=woz_test_psa_ecb \
	"$SRC/ccc/ccc_crypto_psa.c" -o "$OUT/ccc_crypto_psa_host.o"
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags -c \
	-Dcrypto_aes_ecb_encrypt=woz_test_mbedtls_ecb \
	"$SRC/ccc/ccc_crypto_mbedtls.c" -o "$OUT/ccc_crypto_mbedtls_host.o"
# shellcheck disable=SC2086
"${CC:-cc}" "${psa_flags[@]}" $san_flags \
	-I"$HOSTD" -I"$ROOT/modules/woz_aliro/include" \
	"$HOSTD/test.c" "$HOSTD/test_psa_backends.c" "$HOSTD/psafake/psafake.c" \
	"$ROOT/modules/woz_aliro/src/aliro_prim_psa.c" \
	"$OUT/ccc_crypto_psa_host.o" "$OUT/ccc_crypto_mbedtls_host.o" \
	-o "$OUT/host_test_psa"
"$OUT/host_test_psa"

# 3) NFC ECP emitter (C++) over fake RFAL/reader-storage headers (ecpfake/).
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -c "$HOSTD/test.c" -o "$OUT/test_harness_c.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags \
	-DCONFIG_DOOR_LOCK_RFAL_LOG_LEVEL=3 \
	-I"$HOSTD" -I"$HOSTD/ecpfake" \
	"$HOSTD/test_nfc_ecp.cpp" "$ROOT/modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp" \
	"$OUT/test_harness_c.o" \
	-o "$OUT/host_test_ecp"
"$OUT/host_test_ecp"

# 4) DWM3001CDK port glue over a fake settings backend (settingsfake/).
#    Its own binary because the fake <zephyr/settings/settings.h> would collide
#    with the other suites' Zephyr surface, the same reason (1) and (3) are
#    split out. Compiled with -Wall -Wextra rather than the -w the suites above
#    use: this port has never been warning-checked by anything, since the
#    clang-format and clang-tidy gates both cover modules/ only.
#    Real port source, not a host copy -- the point is to test what ships.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra $san_flags \
	-DCONFIG_LOG_DEFAULT_LEVEL=3 \
	-I"$HOSTD" -I"$HOSTD/settingsfake" -I"$HOSTD/logfake" \
	-I"$ROOT/modules/woz_matter/include" -I"$ROOT/firmware/src" \
	"$HOSTD/test.c" "$HOSTD/test_matter_fab_settings.c" \
	"$HOSTD/settingsfake/settingsfake.c" \
	"$ROOT/firmware/src/matter_fab_settings.c" \
	-o "$ROOT/build/host_test_cdk"
"$ROOT/build/host_test_cdk"

# 5) Delta update, both halves, over RAM flash partitions that enforce the nRF
#    driver's word and page alignment rules (dfufake/), the recording PSA
#    (psafake/) and a scripted detools. Its own binary because dfufake's
#    <zephyr/storage/flash_map.h> and <pm_config.h> exist nowhere else, and
#    because the SMP half needs the zcbor/mcumgr doubles in smpfake/.
#    CONFIG_MCUMGR_SMP_LEGACY_RC_BEHAVIOUR is on here so the explicit "rc" key
#    a legacy client expects is compiled and checked.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags \
	-DCONFIG_WOZ_DFU_SMP_IMG=1 -DCONFIG_WOZ_DFU_APPLIER_CHUNK=256 \
	-DCONFIG_MCUMGR_GRP_OS_RESET_HOOK=1 -DCONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME=1 \
	-DCONFIG_MCUMGR_SMP_LEGACY_RC_BEHAVIOUR=1 \
	-I"$HOSTD" -I"$HOSTD/dfufake" -I"$HOSTD/smpfake" -I"$HOSTD/logfake" \
	-I"$HOSTD/psafake" \
	-I"$ROOT/modules/woz_dfu/include" -I"$ROOT/modules/woz_dfu/src" \
	"$HOSTD/test.c" "$HOSTD/test_dfu.c" "$HOSTD/test_dfu_smp.c" \
	"$HOSTD/dfufake/dfufake.c" "$HOSTD/smpfake/smpfake.c" "$HOSTD/psafake/psafake.c" \
	"$ROOT/modules/woz_dfu/src/dfu_receiver.c" \
	"$ROOT/modules/woz_dfu/src/dfu_applier.c" \
	"$ROOT/modules/woz_dfu/src/dfu_smp_img.c" \
	-o "$OUT/host_test_dfu"
"$OUT/host_test_dfu"

# 6) The woz_nfc transport seam (C++), over fake Zephyr SPI/GPIO/kernel and a
#    recording Aliro stack (nfcfake/). pn532.c and pn532_apdu.c link in for
#    real, so the frames really are encoded and parsed by the shipping codec.
#    transport_none.cpp defines the same five symbols as transport_pn532.cpp,
#    so it is renamed on its own compile step with -DWozNfc=WozNfcNone -- a
#    compile flag, not a source edit, exactly as (2) renames the two crypto
#    backends.
NFC_DEF=(-DCONFIG_WOZ_NFC_LOG_LEVEL=3 -DCONFIG_WOZ_NFC_PN532_THREAD_STACK_SIZE=2048
	-DCONFIG_WOZ_NFC_PN532_POLL_PERIOD_MS=200
	-DCONFIG_WOZ_NFC_PN532_EXCHANGE_TIMEOUT_MS=1000)
NFC_INC=(-I"$HOSTD" -I"$HOSTD/nfcfake" -I"$ROOT/modules/woz_nfc/include"
	-I"$ROOT/modules/woz_nfc/src")
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -c "$HOSTD/test.c" -o "$OUT/test_harness_nfc.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -I"$ROOT/modules/woz_nfc/src" \
	-c "$ROOT/modules/woz_nfc/src/pn532.c" -o "$OUT/pn532_nfc.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -I"$ROOT/modules/woz_nfc/src" \
	-c "$ROOT/modules/woz_nfc/src/pn532_apdu.c" -o "$OUT/pn532_apdu_nfc.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags "${NFC_DEF[@]}" "${NFC_INC[@]}" \
	-c "$ROOT/modules/woz_nfc/src/pn532_bus_spi.c" -o "$OUT/pn532_bus_spi.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${NFC_DEF[@]}" "${NFC_INC[@]}" \
	-c "$ROOT/modules/woz_nfc/src/transport_pn532.cpp" -o "$OUT/transport_pn532.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags -DWozNfc=WozNfcNone \
	"${NFC_DEF[@]}" "${NFC_INC[@]}" \
	-c "$ROOT/modules/woz_nfc/src/transport_none.cpp" -o "$OUT/transport_none.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${NFC_INC[@]}" \
	-c "$HOSTD/nfcfake/nfcfake.cpp" -o "$OUT/nfcfake.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${NFC_DEF[@]}" "${NFC_INC[@]}" \
	-c "$HOSTD/test_nfc_transport.cpp" -o "$OUT/test_nfc_transport.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags \
	"$OUT/test_nfc_transport.o" "$OUT/nfcfake.o" "$OUT/test_harness_nfc.o" \
	"$OUT/transport_none.o" "$OUT/transport_pn532.o" "$OUT/pn532_bus_spi.o" \
	"$OUT/pn532_nfc.o" "$OUT/pn532_apdu_nfc.o" \
	-o "$OUT/host_test_nfc"
"$OUT/host_test_nfc"

# 7) uwb_seam.h's engine-less tier. Compiled WITHOUT CONFIG_WOZ_ALIRO, alone:
#    that half of the header is inline bodies, and a header compiled two ways
#    inside one binary maps the same lines twice. See the file for the rest.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags \
	-I"$HOSTD" -I"$HOSTD/shim" -I"$HOSTD/logfake" \
	-I"$SRC/driver" -I"$ROOT/deps/dw3000/platform" \
	"$HOSTD/test.c" "$HOSTD/test_uwb_seam.c" \
	-o "$OUT/host_test_seam"
"$OUT/host_test_seam"

# 8) The Aliro source stack (C++): aliro_stack.cpp and session.cpp over the
#    Nordic Interface API as recording doubles (stackfake/). The protocol
#    codecs beside them are the shipping sources, linked in whole, so every
#    APDU and BLE frame here is built and parsed for real -- only the crypto
#    and the application callbacks are stand-ins. Its own binary because
#    stackfake's <aliro/*.h> are a different Aliro surface from the one
#    ecpfake and nfcfake carry, and all three would collide.
STK="$ROOT/modules/woz_aliro_stack/src"
STK_DEF=(-DCONFIG_NCS_ALIRO_LOG_LEVEL_VALUE=3 -DCONFIG_NCS_ALIRO_BLE_UWB=1
	-DCONFIG_DOOR_LOCK_EXPEDITED_FAST_PHASE=1 -DCONFIG_DOOR_LOCK_STEP_UP_PHASE=1
	-DCONFIG_DOOR_LOCK_BLE_UWB_MAX_SESSIONS=2 -DCONFIG_WOZ_ALIRO_APDU_BUFFER_SIZE=1024
	-DCONFIG_MAX_NUMBER_OF_KPERSISTENT=4
	-DCONFIG_DOOR_LOCK_STORAGE_MAX_STORED_ACCESS_DOCUMENTS=2)
STK_INC=(-I"$HOSTD" -I"$HOSTD/stackfake" -I"$STK" -I"$STK/protocol"
	-I"$ROOT/modules/woz_aliro/include" -I"$ROOT/modules/woz_aliro/src")
STK_OBJS=()
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -c "$HOSTD/test.c" -o "$OUT/test_harness_stack.o"
for stk_src in advertising_core protocol/ble_message protocol/ble_timeout protocol/tlv \
	protocol/nfc_select protocol/nfc_auth protocol/nfc_step_up protocol/access_document; do
	stk_obj="$OUT/stk_$(basename "$stk_src").o"
	# shellcheck disable=SC2086
	"${CC:-cc}" -std=c11 -O1 -w $san_flags "${STK_DEF[@]}" -I"$STK" -I"$STK/protocol" \
		-c "$STK/$stk_src.c" -o "$stk_obj"
	STK_OBJS+=("$stk_obj")
done
# The symmetric crypto is REAL: aliro_hash.c (SHA-256/HMAC/HKDF, pinned by
# test_aliro_hash.c) and the reference AES-GCM in aliro_prim_host.c (pinned by
# test_aliro_crypto.c). Only P-256 stays a stand-in -- this repo has none on host.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -I"$ROOT/modules/woz_aliro/include" \
	-I"$ROOT/modules/woz_aliro/src" -c "$ROOT/modules/woz_aliro/src/aliro_hash.c" \
	-o "$OUT/stk_aliro_hash.o"
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags -I"$ROOT/modules/woz_aliro/include" \
	-I"$ROOT/modules/woz_aliro/src" -c "$ROOT/ports/esp32/test/aliro_prim_host.c" \
	-o "$OUT/stk_aliro_prim_host.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${STK_DEF[@]}" "${STK_INC[@]}" \
	-c "$STK/aliro_stack.cpp" -o "$OUT/stk_aliro_stack.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${STK_DEF[@]}" "${STK_INC[@]}" \
	-c "$STK/session.cpp" -o "$OUT/stk_session.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${STK_DEF[@]}" "${STK_INC[@]}" \
	-c "$HOSTD/stackfake/stackfake.cpp" -o "$OUT/stackfake.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags "${STK_DEF[@]}" "${STK_INC[@]}" \
	-c "$HOSTD/test_aliro_stack.cpp" -o "$OUT/test_aliro_stack.o"
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -O1 -w $san_flags \
	"$OUT/test_aliro_stack.o" "$OUT/stackfake.o" "$OUT/test_harness_stack.o" \
	"$OUT/stk_aliro_stack.o" "$OUT/stk_session.o" "${STK_OBJS[@]}" \
	"$OUT/stk_aliro_hash.o" "$OUT/stk_aliro_prim_host.o" \
	-o "$OUT/host_test_stack"
"$OUT/host_test_stack"

# Host-side tooling tests (pure-stdlib Python; no toolchain involved).
# test_flash_html needs the python-markdown package and skips cleanly without.
# Each suite is folded to one summary row matching the side binaries above;
# the full unittest log is replayed on failure.
py_suite() { # <name> <script> <note>
	local out ran skipped note
	if ! out="$("$PY" "$2" 2>&1)"; then
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
py_suite power-profile "$ROOT/tests/host/test_power_profile.py" "python, power/calibration reduction"
py_suite flight-recorder "$ROOT/tests/host/test_flight_recorder.py" "python, trace/replay tooling"
py_suite gait "$ROOT/tests/host/test_aliro_gait.py" "python, gait feature probe"
py_suite mqtt-bridge "$ROOT/tests/host/test_mqtt_bridge.py" "python, fake paho/serial"
py_suite presence-verify "$ROOT/tests/host/test_presence_verify.py" "python, real P-256 via openssl"
py_suite presence-git "$ROOT/tests/host/test_presence_git.py" "python, throwaway git repos"
py_suite presence-service "$ROOT/tests/host/test_presence_service.py" "python, Unix socket + fake serial"
py_suite presence-runtime "$ROOT/tests/host/test_presence_runtime.py" "python, deterministic transfer archive"
py_suite piv-pin "$ROOT/tests/host/test_piv_pin.py" "python, PIN policy and status handling"
py_suite flash-html "$ROOT/tests/host/test_flash_html.py" "python, needs markdown pkg"
