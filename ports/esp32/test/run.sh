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
cc -std=c11 -O1 -Wall -Wextra \
   -I "$ALIRO/include" -I "$ALIRO/src" \
   "$HERE/test_aliro_device.c" \
   "$ALIRO/src/aliro_device.c" "$ALIRO/src/aliro_device_apdu.c" \
   "$ALIRO/src/aliro_apdu.c" "$ALIRO/src/aliro_crypto.c" "$ALIRO/src/aliro_hash.c" \
   "$HERE/aliro_prim_host.c" -o "$DBIN"
"$DBIN"
# Syntax-check the target-only EC loopback path so it cannot rot in CI (no host
# EC double exists, so it never runs here — the bench build with real aliro_prim
# is the arbiter).
cc -std=c11 -O1 -Wall -Wextra -fsyntax-only -DALIRO_DEVICE_HAVE_EC \
   -I "$ALIRO/include" -I "$ALIRO/src" "$HERE/test_aliro_device.c"
rm -f "$DBIN"

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
echo "== host: bolt-state LED policy =="
MATTER_MAIN="$HERE/../apps/matter-lock/main"
LBIN="$(mktemp -t lock_led.XXXXXX)"
cc -std=c11 -O1 -Wall -Wextra \
   -I "$MATTER_MAIN" \
   "$HERE/test_lock_led.c" "$MATTER_MAIN/lock_led.c" -o "$LBIN"
"$LBIN"
rm -f "$LBIN"

echo
echo "== target: port build + link-seam guard =="
bash "$HERE/verify_port.sh"
