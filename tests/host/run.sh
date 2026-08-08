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
	-I"$SRC/driver" -I"$SRC/ccc" -I"$SRC/fira" -I"$SRC/facade" -I"$ROOT/ports/zephyr/shell" \
	-I"$ROOT/modules/woz_port/include" -I"$ROOT/modules/woz_dw3000/platform" \
	"$HOSTD/test.c" "$HOSTD/drv_main.c" \
	"$HOSTD/test_uwb_min.c" "$HOSTD/test_uwb_isr.c" "$HOSTD/test_uwb_rxdiag.c" \
	"$HOSTD/test_uwb_cirdiag.c" \
	"$HOSTD/test_uwb_selftest.c" "$HOSTD/test_aliro_shell.c" \
	"$HOSTD/shim/drvfake.c" \
	"$ROOT/modules/woz_port/src/osal_host.c" \
	"$SRC/driver/uwb_min.c" "$SRC/driver/uwb_isr.c" "$SRC/driver/uwb_rxdiag.c" \
	"$SRC/driver/uwb_cirdiag.c" \
	"$SRC/driver/uwb_selftest.c" "$ROOT/ports/zephyr/shell/aliro_shell.c" \
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
	-I"$ROOT/modules/woz_matter/include" -I"$ROOT/ports/zephyr/store" \
	"$HOSTD/test.c" "$HOSTD/test_matter_fab_settings.c" \
	"$HOSTD/settingsfake/settingsfake.c" \
	"$ROOT/ports/zephyr/store/matter_fab_settings.c" \
	-o "$ROOT/build/host_test_cdk"
"$ROOT/build/host_test_cdk"

# 5) Delta update, both halves, over the woz_flash host backend (RAM
#    partitions that enforce the nRF driver's word and page alignment rules),
#    the host OSAL's virtual clock, the recording PSA (psafake/) and a scripted
#    detools (dfufake/). Its own binary because the SMP half needs the
#    zcbor/mcumgr doubles in smpfake/. CONFIG_MCUMGR_SMP_LEGACY_RC_BEHAVIOUR is
#    on here so the explicit "rc" key a legacy client expects is compiled and
#    checked.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O1 -w $san_flags \
	-DWOZ_PORT_HOST -DCONFIG_WOZ_DFU_SMP_IMG=1 -DCONFIG_WOZ_DFU_APPLIER_CHUNK=256 \
	-DCONFIG_MCUMGR_GRP_OS_RESET_HOOK=1 -DCONFIG_MCUMGR_GRP_ENUM_DETAILS_NAME=1 \
	-DCONFIG_MCUMGR_SMP_LEGACY_RC_BEHAVIOUR=1 \
	-I"$HOSTD" -I"$HOSTD/dfufake" -I"$HOSTD/smpfake" -I"$HOSTD/logfake" \
	-I"$HOSTD/psafake" -I"$ROOT/modules/woz_port/include" \
	-I"$ROOT/modules/woz_dfu/include" -I"$ROOT/modules/woz_dfu/src" \
	"$HOSTD/test.c" "$HOSTD/test_dfu.c" "$HOSTD/test_dfu_smp.c" \
	"$HOSTD/dfufake/dfufake.c" "$HOSTD/smpfake/smpfake.c" "$HOSTD/psafake/psafake.c" \
	"$ROOT/modules/woz_port/src/osal_host.c" "$ROOT/modules/woz_port/src/flash_host.c" \
	"$ROOT/modules/woz_dfu/src/dfu_crc.c" \
	"$ROOT/modules/woz_dfu/src/dfu_receiver.c" \
	"$ROOT/modules/woz_dfu/src/dfu_applier.c" \
	"$ROOT/ports/zephyr/dfu/dfu_smp_img.c" \
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
	-DCONFIG_WOZ_NFC_PN532_EXCHANGE_TIMEOUT_MS=1000
	-DWOZ_PORT_HOST) # transport_none logs through woz_log.h
NFC_INC=(-I"$HOSTD" -I"$HOSTD/nfcfake" -I"$ROOT/modules/woz_nfc/include"
	-I"$ROOT/modules/woz_nfc/src" -I"$ROOT/modules/woz_port/include")
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
	-c "$ROOT/ports/zephyr/nfc/pn532_bus_spi.c" -o "$OUT/pn532_bus_spi.o"
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
	-I"$SRC/driver" -I"$ROOT/modules/woz_dw3000/platform" \
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
for stk_src in advertising_core protocol/ble_message protocol/ble_timeout \
	protocol/nfc_select protocol/nfc_auth; do
	stk_obj="$OUT/stk_$(basename "$stk_src").o"
	# shellcheck disable=SC2086
	"${CC:-cc}" -std=c11 -O1 -w $san_flags "${STK_DEF[@]}" -I"$STK" -I"$STK/protocol" \
		-I"$ROOT/modules/woz_aliro/include" -c "$STK/$stk_src.c" -o "$stk_obj"
	STK_OBJS+=("$stk_obj")
done
# The step-up wire codecs + DeviceResponse parser are the shared woz_aliro
# sources (one source of protocol truth; session.cpp calls them directly).
for aliro_src in aliro_tlv aliro_stepup_wire aliro_stepup_parse; do
	stk_obj="$OUT/stk_$aliro_src.o"
	# shellcheck disable=SC2086
	"${CC:-cc}" -std=c11 -O1 -w $san_flags -I"$ROOT/modules/woz_aliro/include" \
		-I"$ROOT/modules/woz_aliro/src" \
		-c "$ROOT/modules/woz_aliro/src/$aliro_src.c" -o "$stk_obj"
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
	-I"$ROOT/modules/woz_aliro/src" -c "$ROOT/tests/shared/aliro_prim_host.c" \
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
