#!/usr/bin/env bash
#
# Test entry point for the ESP32 port. Three layers, all hardware-free:
#   - test_port_headers: fast host unit test of the pure port headers.
#   - test_aliro_crypto: host KAT of the Aliro key-schedule core (SHA-256/KDF),
#                        compiled from the same source as the target.
#   - verify_port.sh:    on-target build + --wrap seam + exclusion guard (needs
#                        the ESP-IDF env; skips cleanly without it).
#
# On-target functional tests (Unity on the DW3000 SPI/IRQ path) are deferred:
# they need the DWM3000EVB wired up. See ../../../docs/esp32-bringup.md.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "== host: port headers unit test =="
BIN="$(mktemp -t woz_port_headers.XXXXXX)"
trap 'rm -f "$BIN" "${CBIN:-}"' EXIT
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE/../../../modules/woz_port/include" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   "$HERE/test_port_headers.c" -o "$BIN"
"$BIN"

echo
echo "== host: aliro_crypto key-schedule KAT =="
# The Aliro core is shared with the nRF build; it lives in modules/woz_aliro.
ALIRO="$HERE/../../../modules/woz_aliro"
CBIN="$(mktemp -t aliro_crypto_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_crypto.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_crypto.c" "$ALIRO/src/aliro_advtag.c" \
   "$HERE/aliro_prim_host.c" -o "$CBIN"
"$CBIN"

echo
echo "== host: aliro_assert_ec P-256 binder =="
# aliro_assert.c is backend-free, so its P-256 path is covered in the main host
# suite against a local double. This is the shim to aliro_prim, which only has
# meaning where a prim implementation exists -- here, over the fake curve.
ECBIN="$(mktemp -t aliro_assert_ec.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_assert_ec.c" \
   "$ALIRO/src/aliro_assert.c" "$ALIRO/src/aliro_assert_ec.c" "$ALIRO/src/aliro_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$ECBIN"
"$ECBIN"

echo
echo "== host: aliro_apdu wire-codec KAT =="
ABIN="$(mktemp -t aliro_apdu_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_apdu.c" "$ALIRO/src/aliro_apdu.c" -o "$ABIN"
"$ABIN"
rm -f "$ABIN"

echo
echo "== host: aliro_stepup Access-Document codec + §7.4 verifier KAT =="
SBIN="$(mktemp -t aliro_stepup_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE" -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_stepup.c" \
   "$ALIRO/src/aliro_stepup.c" "$ALIRO/src/aliro_stepup_parse.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_crypto.c" \
   "$HERE/aliro_prim_host.c" -o "$SBIN"
"$SBIN"
rm -f "$SBIN"

echo
echo "== host: aliro_prov identity/trust KAT =="
PBIN="$(mktemp -t aliro_prov_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_prov.c" "$ALIRO/src/aliro_prov.c" -o "$PBIN"
"$PBIN"
rm -f "$PBIN"

echo
echo "== host: aliro_lat walk-up trace (gate on + gate off) =="
# _POSIX_C_SOURCE: woz_port.h's host woz_uptime_us needs clock_gettime /
# CLOCK_MONOTONIC, which strict -std=c11 hides on glibc (macOS exposes them
# regardless). A -D lands before every include, so ordering is safe.
WOZ_PORT_INC="$HERE/../../../modules/woz_port/include"
TBIN="$(mktemp -t aliro_lat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L \
   -DWOZ_PORT_HOST -DCONFIG_ALIRO_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/aliro_lat.c" -o "$TBIN"
"$TBIN"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L \
   -DWOZ_PORT_HOST \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/aliro_lat.c" -o "$TBIN"
"$TBIN"
rm -f "$TBIN"

echo
echo "== host: aliro_reader engine walk-up (scripted phone) =="
# The reader engine end-to-end: a scripted phone drives AUTH0/AUTH1/EXCHANGE/
# AP-Completed against the real state machine + codec + key schedule, with the
# BLE transport, ranging adapter and NVS backend as recording doubles and the
# fake-EC prim double standing in for the curve (see aliro_prim_host.c).
# -Wno-unused-variable/-function: the host LOG no-ops orphan the unit's rc/
# diagnostic locals; the test file itself stays warning-clean.
RBIN="$(mktemp -t aliro_reader.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -Wno-unused-variable -Wno-unused-function \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$WOZ_PORT_INC" \
   "$HERE/test_aliro_reader.c" \
   "$ALIRO/src/aliro_reader.c" "$ALIRO/src/aliro_apdu.c" \
   "$ALIRO/src/aliro_crypto.c" "$ALIRO/src/aliro_hash.c" \
   "$ALIRO/src/aliro_prov.c" \
   "$HERE/aliro_prim_host.c" -o "$RBIN"
"$RBIN"
rm -f "$RBIN"

echo
echo "== host: aliro_ranging M1-M4 session glue =="
# The ranging-setup glue against recording doubles of the engine (cherry/
# adapter/session), the BLE transport and the woz_uwb facade; the BleSK
# sealing in the transmit callback is real crypto, opened by the test with
# the mirrored device-direction GCM.
UWB_SRC="$HERE/../../../modules/woz_uwb/src"
GBIN="$(mktemp -t aliro_ranging.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST -DCONFIG_ALIRO_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$WOZ_PORT_INC" \
   -I "$UWB_SRC/facade" -I "$UWB_SRC/aliro/include" \
   "$HERE/test_aliro_ranging.c" \
   "$ALIRO/src/aliro_ranging.c" "$ALIRO/src/aliro_crypto.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_lat.c" \
   "$HERE/aliro_prim_host.c" -o "$GBIN"
"$GBIN"
rm -f "$GBIN"

echo
echo "== host: bolt-state LED policy =="
MATTER_MAIN="$HERE/../apps/matter-lock/main"
LBIN="$(mktemp -t lock_led.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$MATTER_MAIN" \
   "$HERE/test_lock_led.c" "$MATTER_MAIN/lock_led.c" -o "$LBIN"
"$LBIN"
rm -f "$LBIN"

echo
echo "== host: aliro_ble transport vs NimBLE fakes =="
# Target-only sources compiled against the recording doubles in sdkfake/.
# These suites prove branch logic + wiring against the fakes, not hardware
# truth; the dynamic-tag advert bytes are cross-checked against the KAT'd
# aliro_advtag_derive. See sdkfake/sdkfake.h.
SDKFAKE="$HERE/sdkfake"
EBIN="$(mktemp -t esp_aliro_ble.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$SDKFAKE" -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_esp_aliro_ble.c" \
   "$HERE/../components/aliro_ble/aliro_ble.c" \
   "$ALIRO/src/aliro_advtag.c" "$ALIRO/src/aliro_hash.c" \
   "$HERE/aliro_prim_host.c" \
   "$SDKFAKE/fake_nimble.c" "$SDKFAKE/fake_nvs.c" -o "$EBIN"
"$EBIN"
rm -f "$EBIN"

echo
echo "== host: aliro_prov NVS backend vs in-RAM NVS fake =="
NBIN="$(mktemp -t esp_prov_nvs.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$SDKFAKE" -I "$ALIRO/include" \
   "$HERE/test_esp_prov_nvs.c" \
   "$HERE/../components/aliro_reader/aliro_prov_nvs.c" \
   "$ALIRO/src/aliro_prov.c" \
   "$SDKFAKE/fake_nvs.c" -o "$NBIN"
"$NBIN"
rm -f "$NBIN"

echo
echo "== host: aliro_stepup worker vs FreeRTOS fakes =="
# The queue/task doubles are pumped synchronously; the decrypt/parse/verify
# underneath is the real shared-core code on the stepup_vectors.h KATs.
WBIN="$(mktemp -t esp_stepup_worker.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DCONFIG_WOZ_ALIRO_STEPUP=1 \
   -I "$SDKFAKE" -I "$HERE" -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_esp_stepup_worker.c" \
   "$HERE/../components/aliro_reader/aliro_stepup_worker.c" \
   "$ALIRO/src/aliro_stepup.c" "$ALIRO/src/aliro_stepup_parse.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_crypto.c" \
   "$HERE/aliro_prim_host.c" \
   "$SDKFAKE/fake_freertos.c" -o "$WBIN"
"$WBIN"
rm -f "$WBIN"

echo
echo "== host: demand-driven presence proof freshness =="
# The command loop is real. Reader/UWB doubles expose the epoch boundaries, and
# the real assertion codec proves stale auth/range, wrong credential, far range,
# reset timeout and ambiguous trust all fail before the signer is called.
PLBIN="$(mktemp -t esp_presence_link.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST \
   -DCONFIG_WOZ_PRESENCE_TIMEOUT_MS=1 -DCONFIG_WOZ_PRESENCE_MAX_CM=40 \
   -I "$SDKFAKE" -I "$HERE/../components/aliro_reader" \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$WOZ_PORT_INC" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   "$HERE/test_esp_presence_link.c" \
   "$HERE/../components/aliro_reader/presence_link.c" \
   "$ALIRO/src/aliro_assert.c" "$ALIRO/src/aliro_hash.c" \
   "$SDKFAKE/fake_nvs.c" -o "$PLBIN"
"$PLBIN" | grep -E '^(--|  ok|  FAIL|RESULT)'
rm -f "$PLBIN"

echo
echo "== host: PIV CCID protocol core =="
PIVBIN="$(mktemp -t esp_piv_ccid.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -Werror \
   -I "$HERE/../components/piv_ccid/include" \
   "$HERE/test_piv_ccid.c" \
   "$HERE/../components/piv_ccid/piv_ccid.c" \
   "$HERE/../components/piv_ccid/piv_apdu.c" -o "$PIVBIN"
"$PIVBIN"
rm -f "$PIVBIN"

echo
echo "== host: reader bench console vs esp_console fakes =="
CSBIN="$(mktemp -t esp_app_shell.XXXXXX)"
# _POSIX_C_SOURCE because main.c now includes woz_port.h, whose host build calls
# clock_gettime(CLOCK_MONOTONIC): glibc declares neither without it, while macOS
# declares both unconditionally, so omitting it builds locally and fails on CI.
cc -std=c11 -O1 -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
   -DCONFIG_WOZ_ALIRO_STEPUP=1 -DWOZ_PORT_HOST \
   -I "$SDKFAKE" -I "$HERE/../apps/reader/main" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   "$HERE/test_esp_app_shell.c" \
   "$HERE/../apps/reader/main/app_shell.c" \
   "$HERE/../apps/reader/main/main.c" \
   "$SDKFAKE/fake_freertos.c" "$SDKFAKE/fake_esp.c" -o "$CSBIN"
# pipefail keeps the binary's exit status; the grep hides the handlers' own
# bench printf noise without dropping any ok/FAIL/RESULT line.
"$CSBIN" | grep -E '^(--|  ok|  FAIL|RESULT)'
rm -f "$CSBIN"

echo
echo "== host: DW3000 ESP-IDF backend vs GPIO/SPI fakes (S3 dual-core) =="
DBIN="$(mktemp -t esp_dw3000_port.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DCONFIG_WOZ_UWB_CIRDIAG=1 \
   -DCONFIG_IDF_TARGET_ESP32S3=1 \
   -DCONFIG_FREERTOS_NUMBER_OF_CORES=2 \
   -DCONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240 \
   -I "$SDKFAKE" -I "$HERE/../components/woz_uwb/port" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   -I "$HERE/../../../deps/dw3000/platform" \
   -I "$HERE/../../../deps/dw3000/dwt_uwb_driver" \
   "$HERE/test_esp_dw3000_port.c" \
   "$HERE/../components/woz_uwb/port/dw3000_hw.c" \
   "$HERE/../components/woz_uwb/port/dw3000_spi.c" \
   "$SDKFAKE/fake_driver.c" "$SDKFAKE/fake_freertos.c" -o "$DBIN"
"$DBIN"
rm -f "$DBIN"

echo
echo "== host: DW3000 ESP-IDF backend vs GPIO/SPI fakes (C6 single-core) =="
DBIN="$(mktemp -t esp_dw3000_port_c6.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DCONFIG_WOZ_UWB_CIRDIAG=1 \
   -DCONFIG_IDF_TARGET_ESP32C6=1 \
   -DCONFIG_FREERTOS_NUMBER_OF_CORES=1 \
   -DCONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160 \
   -I "$SDKFAKE" -I "$HERE/../components/woz_uwb/port" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   -I "$HERE/../../../deps/dw3000/platform" \
   -I "$HERE/../../../deps/dw3000/dwt_uwb_driver" \
   "$HERE/test_esp_dw3000_port.c" \
   "$HERE/../components/woz_uwb/port/dw3000_hw.c" \
   "$HERE/../components/woz_uwb/port/dw3000_spi.c" \
   "$SDKFAKE/fake_driver.c" "$SDKFAKE/fake_freertos.c" -o "$DBIN"
"$DBIN"
rm -f "$DBIN"

echo
echo "== host: --wrap RX-callback shim chaining =="
SBIN2="$(mktemp -t esp_wrap_stubs.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DCONFIG_WOZ_UWB_CIRDIAG=1 \
   -I "$HERE/../../../deps/dw3000/dwt_uwb_driver" \
   -I "$HERE/../../../modules/woz_uwb/src/ccc" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   "$HERE/test_esp_wrap_stubs.c" \
   "$HERE/../components/woz_uwb/port/woz_wrap_stubs.c" -o "$SBIN2"
"$SBIN2"
rm -f "$SBIN2"

echo
echo "== host: matter-lock app glue vs CHIP/esp-matter fakes =="
# The six esp-matter door-lock app sources compiled UNMODIFIED against the
# matterfake/ CHIP + esp-matter recording doubles (C++17). Proves branch
# logic + argument plumbing only — never CHIP-stack, NimBLE, hardware, or
# crypto truth. lock_led.c and the shared aliro_approach controller are C, so
# they get their own objects first (app_main.cpp drives the approach controller).
MFAKE="$HERE/matterfake"
LOCKD="$HERE/../apps/matter-lock/main"
MBIN="$(mktemp -t esp_matter_lock.XXXXXX)"
cc -std=c11 -O1 -w -c "$LOCKD/lock_led.c" -o "$MBIN.led.o"
cc -std=c11 -O1 -w -I "$ALIRO/include" -c "$ALIRO/src/aliro_approach.c" -o "$MBIN.approach.o"
${CXX:-c++} -std=c++17 -O1 -w \
   -DCONFIG_ENABLE_ALIRO_BLE_UWB=1 -DCONFIG_WOZ_ALIRO_LAB=1 -DCONFIG_WOZ_UWB_CIRDIAG=1 \
   -DCONFIG_ALIRO_LAT_TRACE=1 -DCONFIG_IDF_TARGET_ESP32C6=1 -DWOZ_PORT_HOST \
   -I "$MFAKE" -I "$SDKFAKE" -I "$LOCKD" -I "$LOCKD/lock" \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   -I "$HERE/../../../modules/woz_uwb/src/facade" \
   "$HERE/test_esp_matter_lock.cpp" \
   "$LOCKD/app_driver.cpp" "$LOCKD/app_main.cpp" "$LOCKD/app_shell.cpp" \
   "$LOCKD/lock/door_lock_manager.cpp" "$LOCKD/lock/door_lock_callbacks.cpp" \
   "$LOCKD/lock/aliro_reader_delegate.cpp" \
   "$MFAKE/matterfake.cc" "$MBIN.led.o" "$MBIN.approach.o" -o "$MBIN"
# pipefail keeps the binary's exit status; the grep hides app_main's own
# onboarding-code printf noise without dropping any ok/FAIL/RESULT line.
"$MBIN" | grep -E '^(--|  ok|  FAIL|RESULT)'
rm -f "$MBIN" "$MBIN.led.o" "$MBIN.approach.o"

echo
echo "== target: port build + link-seam guard =="
bash "$HERE/verify_port.sh"
