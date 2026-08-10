#!/usr/bin/env bash
# Compile the production standalone FreeRTOS OSAL against recording kernel and
# BSP doubles. This proves the port contract without claiming target hardware.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${ALIRO_BUILD_ROOT:-$ROOT/build}/freertos-nrf52833-host"
BIN="$OUT/freertos_port_test"
RADIO_BIN="$OUT/freertos_radio_start_test"

mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_freertos_port.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nimble.c" \
	"$HERE/fake/fake_nrf.c" \
	"$ROOT/ports/freertos-nrf52833/osal/osal_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/ble/nimble_sdc_transport.c" \
	"$ROOT/ports/freertos-nrf52833/radio/mpsl_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/radio/nrf_802154_irq_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/radio/nrf_802154_misc_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/thread/openthread_freertos.c" \
	-o "$BIN"
"$BIN"

# The radio startup sequencer claims one-shot MPSL, controller, and transport
# state, so it gets its own binary and forks one process per failure scenario.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_freertos_radio_start.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nimble.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_sdc.c" \
	"$ROOT/ports/freertos-nrf52833/ble/nimble_sdc_transport.c" \
	"$ROOT/ports/freertos-nrf52833/radio/mpsl_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/radio/radio_start_freertos.c" \
	-o "$RADIO_BIN"
"$RADIO_BIN"
