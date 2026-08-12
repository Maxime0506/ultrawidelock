#!/usr/bin/env bash
# Portable host suites for module contracts and the Aliro core.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ALIRO="$ROOT/modules/ultrawidelock_cred"
ULTRAWIDELOCK_PORT_INC="$ROOT/modules/ultrawidelock_port/include"
UWB_INC="$ROOT/modules/ultrawidelock_uwb/include"
DW3000_INC="$ROOT/modules/ultrawidelock_dw3000/include"

echo "== host: port headers unit test =="
BIN="$(mktemp -t ultrawidelock_port_headers.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ULTRAWIDELOCK_PORT_INC" -I "$UWB_INC" -I "$ALIRO/include" -I "$DW3000_INC" \
   "$HERE/test_port_headers.c" -o "$BIN"
"$BIN"
rm -f "$BIN"

echo
echo "== host: ultrawidelock_crypto key-schedule KAT =="
CBIN="$(mktemp -t aliro_crypto_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_crypto.c" \
   "$ALIRO/src/ultrawidelock_hash.c" "$ALIRO/src/ultrawidelock_crypto.c" "$ALIRO/src/ultrawidelock_advtag.c" \
   "$HERE/aliro_prim_host.c" -o "$CBIN"
"$CBIN"
rm -f "$CBIN"

echo
echo "== host: ultrawidelock_assert_ec P-256 binder =="
ECBIN="$(mktemp -t ultrawidelock_assert_ec.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_assert_ec.c" \
   "$ALIRO/src/ultrawidelock_assert.c" "$ALIRO/src/ultrawidelock_assert_ec.c" "$ALIRO/src/ultrawidelock_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$ECBIN"
"$ECBIN"
rm -f "$ECBIN"

echo
echo "== host: ultrawidelock_apdu wire-codec KAT =="
ABIN="$(mktemp -t aliro_apdu_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_apdu.c" "$ALIRO/src/ultrawidelock_apdu.c" -o "$ABIN"
"$ABIN"
rm -f "$ABIN"

echo
echo "== host: ultrawidelock_device initiator codec + crypto KAT =="
DBIN="$(mktemp -t ultrawidelock_device.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra -DULTRAWIDELOCK_DEVICE_HAVE_EC \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_device.c" \
   "$ALIRO/src/ultrawidelock_device.c" "$ALIRO/src/ultrawidelock_device_apdu.c" \
   "$ALIRO/src/ultrawidelock_apdu.c" "$ALIRO/src/ultrawidelock_crypto.c" "$ALIRO/src/ultrawidelock_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$DBIN"
"$DBIN"
rm -f "$DBIN"

echo
echo "== host: ultrawidelock_ble_central device-transport decoders =="
BCBIN="$(mktemp -t ultrawidelock_ble_central.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" \
   "$HERE/test_aliro_ble_central.c" "$ALIRO/src/ultrawidelock_ble_central.c" -o "$BCBIN"
"$BCBIN"
rm -f "$BCBIN"

echo
echo "== host: ultrawidelock_stepup Access-Document codec + section 7.4 verifier KAT =="
SBIN="$(mktemp -t aliro_stepup_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$HERE" -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_stepup.c" \
   "$ALIRO/src/ultrawidelock_stepup.c" "$ALIRO/src/ultrawidelock_stepup_wire.c" \
   "$ALIRO/src/ultrawidelock_stepup_parse.c" "$ALIRO/src/ultrawidelock_tlv.c" \
   "$ALIRO/src/ultrawidelock_hash.c" "$ALIRO/src/ultrawidelock_crypto.c" \
   "$HERE/aliro_prim_host.c" -o "$SBIN"
"$SBIN"
rm -f "$SBIN"

echo
echo "== host: ultrawidelock_prov identity/trust KAT =="
PBIN="$(mktemp -t aliro_prov_kat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_prov.c" "$ALIRO/src/ultrawidelock_prov.c" -o "$PBIN"
"$PBIN"
rm -f "$PBIN"

echo
echo "== host: ultrawidelock_lat walk-up trace (gate on + gate off) =="
TBIN="$(mktemp -t ultrawidelock_lat.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DULTRAWIDELOCK_PORT_HOST -DCONFIG_ULTRAWIDELOCK_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$ULTRAWIDELOCK_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/ultrawidelock_lat.c" -o "$TBIN"
"$TBIN"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DULTRAWIDELOCK_PORT_HOST \
   -I "$ALIRO/include" -I "$ULTRAWIDELOCK_PORT_INC" \
   "$HERE/test_aliro_lat.c" "$ALIRO/src/ultrawidelock_lat.c" -o "$TBIN"
"$TBIN"
rm -f "$TBIN"

echo
echo "== host: ultrawidelock_reader engine walk-up (scripted phone) =="
RBIN="$(mktemp -t ultrawidelock_reader.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -Wno-unused-variable -Wno-unused-function \
   -D_POSIX_C_SOURCE=200809L -DULTRAWIDELOCK_PORT_HOST \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$ULTRAWIDELOCK_PORT_INC" \
   "$HERE/test_aliro_reader.c" \
   "$ALIRO/src/ultrawidelock_reader.c" "$ALIRO/src/ultrawidelock_apdu.c" \
   "$ALIRO/src/ultrawidelock_crypto.c" "$ALIRO/src/ultrawidelock_hash.c" \
   "$ALIRO/src/ultrawidelock_prov.c" \
   "$HERE/aliro_prim_host.c" -o "$RBIN"
"$RBIN"
rm -f "$RBIN"

echo
echo "== host: ultrawidelock_ranging M1-M4 session glue =="
GBIN="$(mktemp -t ultrawidelock_ranging.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -D_POSIX_C_SOURCE=200809L -DULTRAWIDELOCK_PORT_HOST -DCONFIG_ULTRAWIDELOCK_LAT_TRACE=1 \
   -I "$ALIRO/include" -I "$ALIRO/src" -I "$ULTRAWIDELOCK_PORT_INC" -I "$UWB_INC" \
   "$HERE/test_aliro_ranging.c" \
   "$ALIRO/src/ultrawidelock_ranging.c" "$ALIRO/src/ultrawidelock_crypto.c" \
   "$ALIRO/src/ultrawidelock_hash.c" "$ALIRO/src/ultrawidelock_lat.c" \
   "$HERE/aliro_prim_host.c" -o "$GBIN"
"$GBIN"
rm -f "$GBIN"

# The remaining host suites exercise ESP-owned sources against ESP fakes.
bash "$ROOT/tests/ports/esp32/run.sh"
