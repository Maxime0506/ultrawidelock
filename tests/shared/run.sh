#!/usr/bin/env bash
# Portable host suites for module contracts and the Aliro core.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ALIRO="$ROOT/modules/woz_aliro"
WOZ_PORT_INC="$ROOT/modules/woz_port/include"
UWB_INC="$ROOT/modules/woz_uwb/include"
DW3000_INC="$ROOT/modules/ultrawidelock_dw3000/include"

echo "== host: port headers unit test =="
BIN="$(mktemp -t woz_port_headers.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$WOZ_PORT_INC" -I "$UWB_INC" -I "$ALIRO/include" -I "$DW3000_INC" \
   "$HERE/test_port_headers.c" -o "$BIN"
"$BIN"
rm -f "$BIN"

echo
echo "== host: aliro_crypto key-schedule KAT =="
CBIN="$(mktemp -t aliro_crypto_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_crypto.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_crypto.c" "$ALIRO/src/aliro_advtag.c" \
   "$HERE/aliro_prim_host.c" -o "$CBIN"
"$CBIN"
rm -f "$CBIN"

echo
echo "== host: aliro_assert_ec P-256 binder =="
ECBIN="$(mktemp -t aliro_assert_ec.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_assert_ec.c" \
   "$ALIRO/src/aliro_assert.c" "$ALIRO/src/aliro_assert_ec.c" "$ALIRO/src/aliro_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$ECBIN"
"$ECBIN"
rm -f "$ECBIN"

echo
echo "== host: aliro_apdu wire-codec KAT =="
ABIN="$(mktemp -t aliro_apdu_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_apdu.c" "$ALIRO/src/aliro_apdu.c" -o "$ABIN"
"$ABIN"
rm -f "$ABIN"

echo
echo "== host: aliro_device initiator codec + crypto KAT =="
DBIN="$(mktemp -t aliro_device.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DALIRO_DEVICE_HAVE_EC \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_device.c" \
   "$ALIRO/src/aliro_device.c" "$ALIRO/src/aliro_device_apdu.c" \
   "$ALIRO/src/aliro_apdu.c" "$ALIRO/src/aliro_crypto.c" "$ALIRO/src/aliro_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$DBIN"
"$DBIN"
rm -f "$DBIN"

echo
echo "== host: aliro_ble_central device-transport decoders =="
BCBIN="$(mktemp -t aliro_ble_central.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" \
   "$HERE/test_aliro_ble_central.c" "$ALIRO/src/aliro_ble_central.c" -o "$BCBIN"
"$BCBIN"
rm -f "$BCBIN"

echo
echo "== host: aliro_stepup Access-Document codec + section 7.4 verifier KAT =="
SBIN="$(mktemp -t aliro_stepup_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE" -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_stepup.c" \
   "$ALIRO/src/aliro_stepup.c" "$ALIRO/src/aliro_stepup_wire.c" \
   "$ALIRO/src/aliro_stepup_parse.c" "$ALIRO/src/aliro_tlv.c" \
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
TBIN="$(mktemp -t aliro_lat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST -DCONFIG_ALIRO_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/aliro_lat.c" -o "$TBIN"
"$TBIN"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST \
   -I "$ALIRO/include" -I "$WOZ_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/aliro_lat.c" -o "$TBIN"
"$TBIN"
rm -f "$TBIN"

echo
echo "== host: aliro_reader engine walk-up (scripted phone) =="
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
GBIN="$(mktemp -t aliro_ranging.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DWOZ_PORT_HOST -DCONFIG_ALIRO_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$WOZ_PORT_INC" -I "$UWB_INC" \
   "$HERE/test_aliro_ranging.c" \
   "$ALIRO/src/aliro_ranging.c" "$ALIRO/src/aliro_crypto.c" \
   "$ALIRO/src/aliro_hash.c" "$ALIRO/src/aliro_lat.c" \
   "$HERE/aliro_prim_host.c" -o "$GBIN"
"$GBIN"
rm -f "$GBIN"

# The remaining host suites exercise ESP-owned sources against ESP fakes.
bash "$ROOT/tests/ports/esp32/run.sh"
