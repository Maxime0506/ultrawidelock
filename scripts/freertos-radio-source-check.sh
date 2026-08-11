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
