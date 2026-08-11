#!/usr/bin/env bash
# Compile the production standalone FreeRTOS OSAL against recording kernel and
# BSP doubles. This proves the port contract without claiming target hardware.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${ALIRO_BUILD_ROOT:-$ROOT/build}/freertos-nrf52833-host"
BIN="$OUT/freertos_port_test"
RADIO_BIN="$OUT/freertos_radio_start_test"
HOST_BIN="$OUT/freertos_nimble_host_test"
CLOCK_BIN="$OUT/freertos_802154_clock_test"
LPTIMER_BIN="$OUT/freertos_802154_lptimer_test"

mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/ports/freertos-nrf52833/ble/nimble_syscfg" \
	-I"$HERE/fake/nimble_upstream" \
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
	-I"$ROOT/ports/freertos-nrf52833/ble/nimble_syscfg" \
	-I"$HERE/fake/nimble_upstream" \
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

# The host sequencer starts the radio too, so it also forks per scenario.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/ports/freertos-nrf52833/ble/nimble_syscfg" \
	-I"$HERE/fake/nimble_upstream" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_nimble_host_start.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nimble.c" \
	"$HERE/fake/fake_nimble_host.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_sdc.c" \
	"$ROOT/ports/freertos-nrf52833/ble/hci_dispatcher_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/ble/nimble_host_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/ble/nimble_sdc_transport.c" \
	"$ROOT/ports/freertos-nrf52833/radio/mpsl_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/radio/radio_start_freertos.c" \
	-o "$HOST_BIN"
"$HOST_BIN"

# The 802.15.4 clock platform only depends on the MPSL clock double.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_802154_clock.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_sdc.c" \
	"$ROOT/ports/freertos-nrf52833/radio/nrf_802154_clock_freertos.c" \
	-o "$CLOCK_BIN"
"$CLOCK_BIN"

# The RTC2 low-power timer runs against a register-level RTC model.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_802154_lptimer.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_rtc.c" \
	"$ROOT/ports/freertos-nrf52833/radio/nrf_802154_lptimer_freertos.c" \
	-o "$LPTIMER_BIN"
"$LPTIMER_BIN"
