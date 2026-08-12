#!/usr/bin/env bash
#
# Verify the inspected Qorvo DW3/QM33 SDK as the board, UWB, FreeRTOS, and
# SoftDevice source base for the custom OpenThread platform port. This does not
# claim that Qorvo supplies OpenThread integration; that integration belongs to
# ports/freertos-nrf52833.

set -euo pipefail

EXPECTED_SHA256='ec804092158babe4849d224b05528e7cbf3cfe92a05767df0e8274081341ec9e'

if [ "$#" -ne 1 ]; then
	printf 'usage: %s <DW3_QM33_SDK_1.1.1.zip>\n' "$0" >&2
	exit 2
fi

archive=$1
if [ ! -f "$archive" ]; then
	printf 'platform-check: SDK archive not found\n' >&2
	exit 2
fi

for tool in rg unzip zipinfo; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		printf 'platform-check: required tool is missing: %s\n' "$tool" >&2
		exit 2
	fi
done

if command -v sha256sum >/dev/null 2>&1; then
	actual_sha=$(sha256sum "$archive" | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then
	actual_sha=$(shasum -a 256 "$archive" | awk '{print $1}')
else
	printf 'platform-check: sha256sum or shasum is required\n' >&2
	exit 2
fi

if [ "$actual_sha" != "$EXPECTED_SHA256" ]; then
	printf 'platform-check: archive SHA-256 does not match the inspected v1.1.1 artifact\n' >&2
	printf '  expected: %s\n' "$EXPECTED_SHA256" >&2
	printf '  actual:   %s\n' "$actual_sha" >&2
	exit 2
fi
printf '  ok   archive matches the inspected DW3/QM33 SDK v1.1.1 artifact\n'

members_file=$(mktemp)
trap 'rm -f "$members_file"' EXIT
if ! zipinfo -1 "$archive" >"$members_file"; then
	printf 'platform-check: could not read the SDK archive\n' >&2
	exit 2
fi

require_member() {
	local member=$1

	if ! rg -Fqx "$member" "$members_file"; then
		printf 'platform-check: expected archive member is missing: %s\n' "$member" >&2
		exit 2
	fi
}

require_member 'Projects/FreeRTOS/QANI/DWM3001CDK/project_QANI.cmake'
require_member 'Projects/FreeRTOS/QANI/DWM3001CDK/isr_hs.c'
require_member 'Projects/FreeRTOS/QANI/DWM3001CDK/ProjectDefinition/FreeRTOSConfig.h'
require_member 'Projects/FreeRTOS/QANI/DWM3001CDK/ProjectDefinition/sdk_config.h'
require_member 'Projects/FreeRTOS/QANI/Common/cmakefiles/QANI-FreeRTOS.cmake'
require_member 'Src/Comm/Src/BLE/CMakeLists.txt'
require_member 'SDK_BSP/Nordic/SDK_17_1_0/external/freertos/source/include/FreeRTOS.h'
require_member 'SDK_BSP/Nordic/SDK_17_1_0/modules/nrfx/mdk/nrf52833_xxaa.ld'
require_member 'Libs/dwt_uwb_driver/deca_device_api.h'
require_member 'Libs/dwt_uwb_driver/dw3000/dw3000_device.c'
require_member 'SDK_BSP/Nordic/SDK_17_1_0/components/softdevice/s113/hex/s113_nrf52_7.2.0_softdevice.hex'
require_member 'SDK_BSP/Nordic/SDK_17_1_0/components/softdevice/s113/headers/ble_l2cap.h'
require_member 'SDK_BSP/Nordic/SDK_17_1_0/components/softdevice/s113/headers/nrf_soc.h'

project_cmake=$(unzip -p "$archive" \
	'Projects/FreeRTOS/QANI/DWM3001CDK/project_QANI.cmake')
ble_cmake=$(unzip -p "$archive" 'Src/Comm/Src/BLE/CMakeLists.txt')
qani_cmake=$(unzip -p "$archive" \
	'Projects/FreeRTOS/QANI/Common/cmakefiles/QANI-FreeRTOS.cmake')
build_graph="${project_cmake}${ble_cmake}${qani_cmake}"
l2cap_header=$(unzip -p "$archive" \
	'SDK_BSP/Nordic/SDK_17_1_0/components/softdevice/s113/headers/ble_l2cap.h')
soc_header=$(unzip -p "$archive" \
	'SDK_BSP/Nordic/SDK_17_1_0/components/softdevice/s113/headers/nrf_soc.h')
freertos_config=$(unzip -p "$archive" \
	'Projects/FreeRTOS/QANI/DWM3001CDK/ProjectDefinition/FreeRTOSConfig.h')
sdk_config=$(unzip -p "$archive" \
	'Projects/FreeRTOS/QANI/DWM3001CDK/ProjectDefinition/sdk_config.h')
isr_stubs=$(unzip -p "$archive" 'Projects/FreeRTOS/QANI/DWM3001CDK/isr_hs.c')

if ! printf '%s\n' "$project_cmake" | rg -q -- '-DS113'; then
	printf 'platform-check: DWM3001CDK QANI no longer selects SoftDevice S113\n' >&2
	exit 2
fi
if ! printf '%s\n' "$ble_cmake" | rg -q 'nrf_ble_gatt\.c'; then
	printf 'platform-check: DWM3001CDK QANI no longer builds the expected GATT host\n' >&2
	exit 2
fi
printf '  ok   DWM3001CDK FreeRTOS QANI selects SoftDevice S113 and GATT\n'

if ! printf '%s\n' "$l2cap_header" | rg -q 'sd_ble_l2cap_ch_(setup|rx|tx)'; then
	printf 'platform-check: S113 L2CAP CoC API is missing\n' >&2
	exit 2
fi
printf '  ok   bundled S113 exposes L2CAP CoC setup, receive, and transmit APIs\n'

if ! printf '%s\n' "$soc_header" | rg -q 'sd_radio_session_open' ||
	! printf '%s\n' "$soc_header" | rg -q 'sd_radio_request'; then
	printf 'platform-check: S113 radio Timeslot API is missing\n' >&2
	exit 2
fi
printf '  ok   bundled S113 exposes the radio Timeslot API for an interim coexistence port\n'

if ! printf '%s\n' "$freertos_config" | rg -Fq '#define xPortSysTickHandler RTC1_IRQHandler' ||
	! printf '%s\n' "$sdk_config" | rg -Uq '#define NRFX_RTC2_ENABLED\s+1' ||
	! printf '%s\n' "$sdk_config" | rg -Uq '#define APP_TIMER_ENABLED\s+1'; then
	printf 'platform-check: Qorvo RTC ownership no longer matches the inspected baseline\n' >&2
	exit 2
fi
for handler in RADIO_IRQHandler RTC2_IRQHandler SWI0_EGU0_IRQHandler SWI5_EGU5_IRQHandler \
	TIMER1_IRQHandler; do
	if ! printf '%s\n' "$isr_stubs" | rg -q "weak.*void ${handler}"; then
		printf 'platform-check: expected weak vector stub is missing: %s\n' "$handler" >&2
		exit 2
	fi
done
printf '  ok   RTC1 is the FreeRTOS tick and radio vectors are weak link-time handoffs\n'
printf '  ok   RTC2 can move from Qorvo app_timer to the nRF 802.15.4 low-power timer\n'

if rg -qi 'openthread|nrf5.sdk.for.thread|thread.and.zigbee|examples/thread/' \
	"$members_file"; then
	printf '  note archive contains a Thread stack; the port still pins its own version\n'
else
	printf '  todo add the separately pinned OpenThread MTD to the target build graph\n'
fi

if printf '%s\n' "$build_graph" | rg -qi 'openthread|nrf_802154|802_15_4'; then
	printf '  note QANI already contains a radio integration dependency\n'
else
	printf '  todo integrate OpenThread and the MPSL-arbitrated nRF 802.15.4 radio path\n'
fi

if printf '%s\n' "$build_graph" | rg -qi 'l2cap|sd_ble_l2cap'; then
	printf '  note QANI already integrates L2CAP CoC\n'
else
	printf '  todo implement the Aliro/Matter L2CAP CoC backend on the selected BLE host\n'
fi

printf 'RESULT: SOURCE BASE ACCEPTED; custom radio integration and board proof remain\n'
