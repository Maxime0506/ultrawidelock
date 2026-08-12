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
OT_KERNEL_BIN="$OUT/freertos_ot_kernel_test"
OT_RADIO_BIN="$OUT/freertos_ot_radio_test"
OT_ALARM_BIN="$OUT/freertos_ot_alarm_test"
OT_MISC_BIN="$OUT/freertos_ot_misc_test"
KV_BIN="$OUT/freertos_kv_flash_test"
PROV_BIN="$OUT/freertos_prov_kv_test"
BOARD_TIME_BIN="$OUT/freertos_board_time_test"
BOARD_ENTROPY_BIN="$OUT/freertos_board_entropy_test"
BOARD_LOG_BIN="$OUT/freertos_board_log_test"
BOARD_FLASH_BIN="$OUT/freertos_board_flash_test"
CRYPTO_BIN="$OUT/freertos_crypto_backend_test"

mkdir -p "$OUT"

# The FromISR ceiling is stated in two places -- the target configuration and
# the host fake -- so that radio/radio_start_freertos.c's static assertion is
# live on the host. A silent disagreement would leave the host suite checking a
# number the firmware does not use, so they are compared here rather than
# trusted.
real_ceiling=$(sed -n 's/^#define configMAX_SYSCALL_INTERRUPT_PRIORITY[[:space:]]*\([0-9]*\).*/\1/p' \
	"$ROOT/ports/freertos-nrf52833/board/FreeRTOSConfig.h")
fake_ceiling=$(sed -n 's/^#define configMAX_SYSCALL_INTERRUPT_PRIORITY[[:space:]]*\([0-9]*\).*/\1/p' \
	"$HERE/fake/FreeRTOS.h")
if [ -z "$real_ceiling" ] || [ "$real_ceiling" != "$fake_ceiling" ]; then
	printf 'freertos-port-test: configMAX_SYSCALL_INTERRUPT_PRIORITY disagrees: target=%s host=%s\n' \
		"${real_ceiling:-unset}" "${fake_ceiling:-unset}" >&2
	exit 1
fi
printf '  ok   the host FromISR ceiling matches board/FreeRTOSConfig.h (%s)\n' "$real_ceiling"
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
	"$HERE/fake/fake_ppi.c" \
	"$ROOT/ports/freertos-nrf52833/radio/nrf_802154_lptimer_freertos.c" \
	-o "$LPTIMER_BIN"
"$LPTIMER_BIN"

# The Zephyr kernel objects the pinned OpenThread radio platform uses.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/thread/ot_compat" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_ot_kernel.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nrf.c" \
	"$ROOT/ports/freertos-nrf52833/thread/ot_kernel_freertos.c" \
	-o "$OT_KERNEL_BIN"
"$OT_KERNEL_BIN"

# Starting and servicing the pinned OpenThread radio platform.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_ot_radio.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nrf.c" \
	"$ROOT/ports/freertos-nrf52833/thread/ot_radio_freertos.c" \
	-o "$OT_RADIO_BIN"
"$OT_RADIO_BIN"

# OpenThread's millisecond alarm on the port's delayable work.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_port/include" \
	"$HERE/test_ot_alarm.c" \
	"$ROOT/ports/freertos-nrf52833/thread/ot_alarm_freertos.c" \
	-o "$OT_ALARM_BIN"
"$OT_ALARM_BIN"

# OpenThread's entropy, reset, and assertion platform.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_ot_misc.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_power.c" \
	"$ROOT/ports/freertos-nrf52833/thread/ot_misc_freertos.c" \
	-o "$OT_MISC_BIN"
"$OT_MISC_BIN"

# The persistent key-value store, against a flash model that enforces the
# part's own rules. The reboot scenarios fork, because the store's state is
# static and a remount needs fresh statics over the same flash.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_kv_flash.c" \
	"$HERE/fake/fake_flash.c" \
	"$ROOT/ports/freertos-nrf52833/storage/kv_flash_freertos.c" \
	-o "$KV_BIN"
"$KV_BIN"

# The Aliro provisioning backend, over the real store rather than a stub: the
# property under test is that a provisioned identity survives a reset, and only
# the real store can be wrong about that.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/modules/woz_aliro/include" \
	"$HERE/test_prov_kv.c" \
	"$HERE/fake/fake_flash.c" \
	"$ROOT/modules/woz_aliro/src/aliro_prov.c" \
	"$ROOT/ports/freertos-nrf52833/storage/aliro_prov_kv.c" \
	"$ROOT/ports/freertos-nrf52833/storage/kv_flash_freertos.c" \
	-o "$PROV_BIN"
"$PROV_BIN"

# The board's time and fault hooks, against a free-running RTC model.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_board_time.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_rtc.c" \
	"$ROOT/ports/freertos-nrf52833/board/time_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/board/fault_freertos.c" \
	-o "$BOARD_TIME_BIN"
"$BOARD_TIME_BIN"

# The board's entropy pool and die-temperature hook. Each scenario forks: the
# pool is static, and resetting the peripheral model under it would leave the
# port believing it had started a generator that is now stopped.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_board_entropy.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_rng.c" \
	"$HERE/fake/fake_mpsl_temp.c" \
	"$ROOT/ports/freertos-nrf52833/board/entropy_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/board/temperature_freertos.c" \
	-o "$BOARD_ENTROPY_BIN"
"$BOARD_ENTROPY_BIN"

# The board's RTT log sink. The test stands in for the J-Link: it reads the ring
# the way a host does, so what is checked is the buffer a real host would see.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_board_log.c" \
	"$HERE/fake/fake_nrf.c" \
	"$ROOT/ports/freertos-nrf52833/board/log_rtt_freertos.c" \
	-o "$BOARD_LOG_BIN"
"$BOARD_LOG_BIN"

# The board's NVMC flash hooks. Reads are memory-mapped on the part, so the
# model's array stands in for that mapping; writes and erases go through the
# controller model, which enforces its modes and its only-clear-bits rule.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	"-DWOZ_FREERTOS_FLASH_MAPPED(offset)=((const void *)&fake_nvmc_flash[(offset)])" \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	"$HERE/test_board_flash.c" \
	"$HERE/fake/fake_nvmc.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_timer0.c" \
	"$HERE/fake/fake_timeslot.c" \
	"$HERE/fake/fake_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/board/flash_freertos.c" \
	-o "$BOARD_FLASH_BIN"
"$BOARD_FLASH_BIN"

# The crypto backend: Mbed TLS's threading callbacks, the FreeRTOS-heap
# allocator, and the hardware entropy poll. The mutex model enforces exclusion,
# and the PSA double refuses to succeed unless the threading callbacks were
# installed before it ran, so the bring-up order is checked rather than assumed.
# The crypto directory is on the include path because Mbed TLS includes
# "threading_alt.h" by that bare name.
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
	-DWOZ_PORT_FREERTOS \
	-I"$HERE/fake" \
	-I"$ROOT/ports/freertos-nrf52833/include" \
	-I"$ROOT/ports/freertos-nrf52833/crypto" \
	"$HERE/test_crypto_backend.c" \
	"$HERE/fake/fake_nrf.c" \
	"$HERE/fake/fake_freertos.c" \
	"$HERE/fake/fake_mbedtls.c" \
	"$ROOT/ports/freertos-nrf52833/crypto/mbedtls_threading_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/crypto/mbedtls_platform_freertos.c" \
	"$ROOT/ports/freertos-nrf52833/crypto/crypto_init_freertos.c" \
	-o "$CRYPTO_BIN"
"$CRYPTO_BIN"
