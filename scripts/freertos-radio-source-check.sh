#!/usr/bin/env bash
# Verify the exact NCS-derived OpenThread and linkable Nordic radio source set
# selected in ports/freertos-nrf52833/platform.lock.yml.
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'usage: %s <ncs-workspace>\n' "$0" >&2
	exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
lock="$root/ports/freertos-nrf52833/platform.lock.yml"
workspace=$1
openthread="$workspace/modules/lib/openthread"
nrfxlib="$workspace/nrfxlib"
ncs_nrf="$workspace/nrf"
hal_nordic="$workspace/modules/hal/nordic"

lock_value() {
	awk -v section="$1" -v key="$2" '
		$0 == section ":" { active = 1; next }
		active && $0 !~ /^ / { exit }
		active && $1 == key ":" { print $2; exit }
	' "$lock"
}

require_revision() {
	local name=$1
	local path=$2
	local expected=$3
	local actual

	if [ ! -d "$path/.git" ] && ! git -C "$path" rev-parse --git-dir >/dev/null 2>&1; then
		printf 'radio-source-check: %s checkout is missing\n' "$name" >&2
		exit 2
	fi
	actual=$(git -C "$path" rev-parse HEAD)
	if [ "$actual" != "$expected" ]; then
		printf 'radio-source-check: %s revision does not match platform.lock.yml\n' "$name" >&2
		exit 2
	fi
	printf '  ok   %s revision matches platform.lock.yml\n' "$name"
}

require_file() {
	if [ ! -f "$1" ]; then
		printf 'radio-source-check: required source component is missing: %s\n' "$2" >&2
		exit 2
	fi
}

require_revision openthread "$openthread" "$(lock_value openthread revision)"
require_revision nrfxlib "$nrfxlib" "$(lock_value nrfxlib revision)"
require_revision ncs_sdk_nrf "$ncs_nrf" "$(lock_value ncs_sdk_nrf revision)"
require_revision hal_nordic "$hal_nordic" "$(lock_value hal_nordic revision)"

require_file "$openthread/src/core/instance/instance.cpp" 'OpenThread core'
require_file "$nrfxlib/mpsl/lib/nrf52/hard-float/libmpsl.a" 'nRF52 MPSL library'
require_file "$nrfxlib/softdevice_controller/lib/nrf52/hard-float/libsoftdevice_controller_peripheral.a" \
	'nRF52 SoftDevice Controller peripheral library'
require_file "$nrfxlib/nrf_802154/sl/sl/lib/nrf52833/hard-float/libnrf-802154-sl.a" \
	'nRF52833 802.15.4 service-layer library'
require_file "$ncs_nrf/subsys/bluetooth/controller/hci_internal.c" \
	'SoftDevice Controller HCI opcode dispatcher'
require_file "$ncs_nrf/subsys/bluetooth/controller/hci_internal_wrappers.c" \
	'SoftDevice Controller HCI command wrappers'
require_file "$hal_nordic/nrfx/bsp/stable/mdk/nrf52833.h" 'nRF52833 HAL'

if ! rg -Uq '#define NRF_802154_EGU_INSTANCE_NO\s+0' \
	"$nrfxlib/nrf_802154/driver/src/nrf_802154_peripherals_nrf52.h" || \
	! rg -Uq '#define NRF_802154_RTC_INSTANCE_NO\s+2' \
	"$nrfxlib/nrf_802154/driver/src/nrf_802154_peripherals_nrf52.h" || \
	! rg -Uq '#define NRF_802154_HIGH_PRECISION_TIMER_INSTANCE_NO\s+1' \
	"$nrfxlib/nrf_802154/sl/sl/include/nrf_802154_sl_periphs.h"; then
	printf 'radio-source-check: nRF 802.15.4 peripheral defaults changed\n' >&2
	exit 2
fi

if ! rg -Fq 'SDC_HCI_MSG_TYPE_DATA = 0x02' \
	"$nrfxlib/softdevice_controller/include/sdc_hci.h" || \
	! rg -Fq 'SDC_HCI_MSG_TYPE_EVT  = 0x04' \
	"$nrfxlib/softdevice_controller/include/sdc_hci.h" || \
	! rg -Fq '#define HCI_MSG_BUFFER_MAX_SIZE' \
	"$nrfxlib/softdevice_controller/include/sdc_hci.h"; then
	printf 'radio-source-check: SDC HCI packet contract changed\n' >&2
	exit 2
fi

# The startup sequencer in radio/radio_start_freertos.c calls these directly,
# and the host test doubles reproduce these exact signatures.
for symbol in sdc_init sdc_cfg_set sdc_enable sdc_support_helper \
	sdc_support_adv sdc_support_peripheral sdc_support_dle_peripheral \
	sdc_support_le_2m_phy sdc_support_phy_update_peripheral; do
	if ! rg -q "[[:space:]]${symbol}\(" "$nrfxlib/softdevice_controller/include/sdc.h"; then
		printf 'radio-source-check: SoftDevice Controller API is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
if ! rg -Fq 'sdc_rand_source_register' "$nrfxlib/softdevice_controller/include/sdc_soc.h" || \
	! rg -Fq '#define NRF_EAGAIN' "$nrfxlib/softdevice_controller/include/nrf_errno.h"; then
	printf 'radio-source-check: SoftDevice Controller entropy or errno contract changed\n' >&2
	exit 2
fi
if ! rg -q '[[:space:]]mpsl_init\(' "$nrfxlib/mpsl/include/mpsl.h" || \
	! rg -q '[[:space:]]mpsl_low_priority_process\(' "$nrfxlib/mpsl/include/mpsl.h" || \
	! rg -Fq 'MPSL_CLOCK_LF_SRC_XTAL' "$nrfxlib/mpsl/include/mpsl_clock.h"; then
	printf 'radio-source-check: MPSL init or clock contract changed\n' >&2
	exit 2
fi
printf '  ok   MPSL init and SoftDevice Controller startup APIs match the port\n'

# board/temperature_freertos.c reads the die temperature through MPSL rather
# than the TEMP peripheral, because MPSL owns TEMP and takes its own readings to
# calibrate the low-frequency clock. Nothing in this repository can confirm that
# API exists, so it is asserted here against the pinned tree.
if ! rg -q '[[:space:]]mpsl_temperature_get\(' "$nrfxlib/mpsl/include/mpsl_temp.h"; then
	printf 'radio-source-check: MPSL temperature API is missing or moved\n' >&2
	exit 2
fi
printf '  ok   MPSL supplies the die temperature the board hook reads\n'

# board/entropy_freertos.c drives the RNG through the Nordic HAL. These are the
# exact entry points it calls; a HAL bump that renames one has to be seen here
# rather than at the first target link.
for symbol in nrf_rng_task_trigger nrf_rng_event_clear nrf_rng_event_check \
	nrf_rng_int_enable nrf_rng_error_correction_enable nrf_rng_random_value_get; do
	if ! rg -q "[[:space:]]${symbol}\(" "$hal_nordic/nrfx/hal/nrf_rng.h"; then
		printf 'radio-source-check: Nordic RNG HAL entry point is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
if ! rg -Fq 'NRF_RNG_INT_VALRDY_MASK' "$hal_nordic/nrfx/hal/nrf_rng.h" || \
	! rg -Fq 'NRF_RNG_EVENT_VALRDY' "$hal_nordic/nrfx/hal/nrf_rng.h"; then
	printf 'radio-source-check: Nordic RNG HAL event or interrupt mask changed\n' >&2
	exit 2
fi
printf '  ok   Nordic RNG HAL matches the board entropy pool\n'

# board/flash_freertos.c drives NVMC through the Nordic HAL. Same reasoning as
# the RNG above: these are the exact entry points it calls.
for symbol in nrf_nvmc_mode_set nrf_nvmc_ready_check nrf_nvmc_word_write \
	nrf_nvmc_page_erase_start; do
	if ! rg -q "[[:space:]]${symbol}\(" "$hal_nordic/nrfx/hal/nrf_nvmc.h"; then
		printf 'radio-source-check: Nordic NVMC HAL entry point is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
for symbol in NRF_NVMC_MODE_READONLY NRF_NVMC_MODE_WRITE NRF_NVMC_MODE_ERASE; do
	if ! rg -Fq "$symbol" "$hal_nordic/nrfx/hal/nrf_nvmc.h"; then
		printf 'radio-source-check: Nordic NVMC mode is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
printf '  ok   Nordic NVMC HAL matches the board flash hooks\n'

# radio/nrf_802154_clock_freertos.c deliberately avoids the older
# mpsl_clock_hfclk_request/release/is_running trio, which the pinned MPSL marks
# deprecated and slates for removal. Check that the replacements exist and that
# the old ones are still the deprecated spelling, so a version bump that
# reverses either decision is reported.
for symbol in mpsl_clock_hfclk_src_request mpsl_clock_hfclk_src_release \
	mpsl_clock_hfclk_src_is_running; do
	if ! rg -q "[[:space:]]${symbol}\(" "$nrfxlib/mpsl/include/mpsl_clock.h"; then
		printf 'radio-source-check: MPSL clock source API is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
if ! rg -Fq 'MPSL_CLOCK_HF_SRC_XO' "$nrfxlib/mpsl/include/mpsl_clock.h" || \
	! rg -Fq 'MPSL_CLOCK_EVT_HFCLK_STARTED' "$nrfxlib/mpsl/include/mpsl_clock.h"; then
	printf 'radio-source-check: MPSL high-frequency clock source or event enum changed\n' >&2
	exit 2
fi
# The 802.15.4 clock contract the port implements.
for symbol in nrf_802154_clock_init nrf_802154_clock_deinit nrf_802154_clock_hfclk_start \
	nrf_802154_clock_hfclk_stop nrf_802154_clock_hfclk_is_running \
	nrf_802154_clock_lfclk_start nrf_802154_clock_lfclk_stop \
	nrf_802154_clock_lfclk_is_running nrf_802154_clock_hfclk_ready \
	nrf_802154_clock_lfclk_ready; do
	if ! rg -q "[[:space:]]${symbol}\(" \
		"$nrfxlib/nrf_802154/sl/include/platform/nrf_802154_clock.h"; then
		printf 'radio-source-check: nRF 802.15.4 clock contract is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
printf '  ok   nRF 802.15.4 clock contract and MPSL clock source API match the port\n'

# radio/nrf_802154_lptimer_freertos.c implements this contract on RTC2.
lptimer="$nrfxlib/nrf_802154/sl/include/platform/nrf_802154_platform_sl_lptimer.h"
for symbol in nrf_802154_platform_sl_lp_timer_init nrf_802154_platform_sl_lp_timer_deinit \
	nrf_802154_platform_sl_lptimer_current_lpticks_get \
	nrf_802154_platform_sl_lptimer_us_to_lpticks_convert \
	nrf_802154_platform_sl_lptimer_lpticks_to_us_convert \
	nrf_802154_platform_sl_lptimer_schedule_at nrf_802154_platform_sl_lptimer_disable \
	nrf_802154_platform_sl_lptimer_critical_section_enter \
	nrf_802154_platform_sl_lptimer_critical_section_exit \
	nrf_802154_platform_sl_lptimer_hw_task_prepare \
	nrf_802154_platform_sl_lptimer_hw_task_cleanup \
	nrf_802154_platform_sl_lptimer_hw_task_update_ppi \
	nrf_802154_platform_sl_lptimer_sync_schedule_now \
	nrf_802154_platform_sl_lptimer_sync_schedule_at \
	nrf_802154_platform_sl_lptimer_sync_abort \
	nrf_802154_platform_sl_lptimer_sync_event_get \
	nrf_802154_platform_sl_lptimer_sync_lpticks_get \
	nrf_802154_platform_sl_lptimer_granularity_get \
	nrf_802154_sl_timer_handler nrf_802154_sl_timestamper_synchronized; do
	if ! rg -q "[[:space:]]${symbol}\(" "$lptimer"; then
		printf 'radio-source-check: 802.15.4 low-power timer contract is missing: %s\n' \
			"$symbol" >&2
		exit 2
	fi
done
# Every answer the port gives has to stay a value the contract defines, and the
# hardware task has to keep taking its channel from the caller: the 802.15.4
# driver core owns the PPI allocation, so the port never picks a channel and
# cannot collide with MPSL.
for macro in NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS \
	NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE \
	NRF_802154_SL_LPTIMER_PLATFORM_TOO_DISTANT \
	NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES \
	NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE \
	NRF_802154_SL_HW_TASK_PPI_INVALID; do
	if ! rg -Fq "$macro" "$lptimer"; then
		printf 'radio-source-check: 802.15.4 low-power timer result code is missing: %s\n' \
			"$macro" >&2
		exit 2
	fi
done
printf '  ok   nRF 802.15.4 low-power timer contract matches the RTC2 platform\n'

# The timestamper platform is a cross-domain abstraction for the parts that
# have one, and nRF52833 does not: the service layer binary for this part asks
# for none of it. Implementing it here would be writing code nothing calls, so
# the claim is checked rather than assumed.
sl_lib="$nrfxlib/nrf_802154/sl/sl/lib/nrf52833/soft-float/libnrf-802154-sl.a"
require_file "$sl_lib" 'nRF52833 802.15.4 service layer library'
if nm -u "$sl_lib" 2>/dev/null | rg -q 'nrf_802154_platform_timestamper'; then
	printf 'radio-source-check: the nRF52833 service layer now needs the timestamper platform\n' >&2
	exit 2
fi
# The same library is what names the platform this port has to satisfy, so an
# entry point appearing there that nothing implements has to be caught here.
for symbol in nrf_802154_platform_sl_lptimer_hw_task_prepare \
	nrf_802154_platform_sl_lptimer_hw_task_cleanup \
	nrf_802154_platform_sl_lptimer_hw_task_update_ppi; do
	if ! nm -u "$sl_lib" 2>/dev/null | rg -q "[[:space:]]${symbol}\$"; then
		printf 'radio-source-check: the service layer no longer calls: %s\n' "$symbol" >&2
		exit 2
	fi
done
printf '  ok   the nRF52833 service layer calls the hardware task and no timestamper\n'

# thread/ot_alarm_freertos.c implements this contract; the host test builds
# against a reproduced header, so the names are checked against the real one.
ot_alarm="$workspace/modules/lib/openthread/include/openthread/platform/alarm-milli.h"
require_file "$ot_alarm" 'OpenThread millisecond alarm contract'
for symbol in otPlatAlarmMilliStartAt otPlatAlarmMilliStop otPlatAlarmMilliGetNow \
	otPlatAlarmMilliFired; do
	if ! rg -q "[[:space:]]${symbol}\(" "$ot_alarm"; then
		printf 'radio-source-check: OpenThread alarm contract is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
# The microsecond alarm is deliberately not implemented. It only exists when
# OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE is set, and this product leaves
# it at upstream's default of 0: a receiver-on MED with no CSL and no time sync.
if ! rg -Fq '#define OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE 0' \
	"$workspace/modules/lib/openthread/src/core/config/platform.h"; then
	printf 'radio-source-check: the OpenThread microsecond timer is no longer off by default\n' >&2
	exit 2
fi
printf '  ok   OpenThread alarm contract matches the port, microsecond timer still off\n'

# thread/ot_misc_freertos.c implements these, and returns error codes by value
# through a reproduced header, so the values are checked against the real one.
ot_misc="$workspace/modules/lib/openthread/include/openthread/platform/misc.h"
require_file "$ot_misc" 'OpenThread reset and assertion contract'
for symbol in otPlatReset otPlatResetToBootloader otPlatGetResetReason otPlatAssertFail; do
	if ! rg -q "[[:space:]]${symbol}\(" "$ot_misc"; then
		printf 'radio-source-check: OpenThread misc contract is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
if ! rg -q '[[:space:]]otPlatEntropyGet\(' \
	"$workspace/modules/lib/openthread/include/openthread/platform/entropy.h"; then
	printf 'radio-source-check: OpenThread entropy contract is missing\n' >&2
	exit 2
fi
ot_error="$workspace/modules/lib/openthread/include/openthread/error.h"
for pair in 'OT_ERROR_NONE = 0' 'OT_ERROR_FAILED = 1' 'OT_ERROR_INVALID_ARGS = 7' \
	'OT_ERROR_NOT_CAPABLE = 27' ; do
	if ! rg -Fq "$pair" "$ot_error"; then
		printf 'radio-source-check: OpenThread error value moved: %s\n' "$pair" >&2
		exit 2
	fi
done
for pair in 'OT_PLAT_RESET_REASON_POWER_ON = 0' 'OT_PLAT_RESET_REASON_EXTERNAL = 1' \
	'OT_PLAT_RESET_REASON_SOFTWARE = 2' 'OT_PLAT_RESET_REASON_FAULT    = 3' \
	'OT_PLAT_RESET_REASON_OTHER    = 6' 'OT_PLAT_RESET_REASON_WATCHDOG = 8'; do
	if ! rg -Fq "$pair" "$ot_misc"; then
		printf 'radio-source-check: OpenThread reset reason moved: %s\n' "$pair" >&2
		exit 2
	fi
done
printf '  ok   OpenThread entropy, reset and error contracts match the port\n'

if ! rg -Fq 'valid for both RTOS and RTOS-free environments' \
	"$nrfxlib/mpsl/doc/mpsl.rst"; then
	printf 'radio-source-check: MPSL no longer documents RTOS-independent integration\n' >&2
	exit 2
fi
if ! rg -Fq 'RTOS-agnostic library' \
	"$nrfxlib/softdevice_controller/doc/softdevice_controller.rst"; then
	printf 'radio-source-check: SoftDevice Controller no longer documents an RTOS-agnostic API\n' >&2
	exit 2
fi
if ! rg -Fq 'Using other build systems' \
	"$nrfxlib/nrf_802154/doc/rd_including.rst"; then
	printf 'radio-source-check: nRF 802.15.4 non-NCS integration instructions are missing\n' >&2
	exit 2
fi

printf '  ok   MPSL and SoftDevice Controller document custom RTOS integration\n'
printf '  ok   SDC HCI packet contract and pinned opcode dispatcher are present\n'
printf '  ok   nRF 802.15.4 defaults retain EGU0, RTC2, and TIMER1 ownership\n'
printf '  ok   nRF52833 802.15.4 service layer and non-NCS build contract are present\n'
printf 'RESULT: PINNED RADIO SOURCE SET ACCEPTED; FreeRTOS glue and board proof remain\n'
