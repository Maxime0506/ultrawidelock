#!/usr/bin/env bash
# Verify the pinned Apache NimBLE host exposes the exact FreeRTOS and L2CAP CoC
# surfaces required by the Aliro and Matter BLE adapters.
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'usage: %s <mynewt-nimble-checkout>\n' "$0" >&2
	exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
lock="$root/ports/freertos-nrf52833/platform.lock.yml"
nimble=$1

lock_value() {
	awk -v section="$1" -v key="$2" '
		$0 == section ":" { active = 1; next }
		active && $0 !~ /^ / { exit }
		active && $1 == key ":" { print $2; exit }
	' "$lock"
}

expected=$(lock_value nimble revision)
if ! git -C "$nimble" rev-parse --git-dir >/dev/null 2>&1; then
	printf 'ble-source-check: NimBLE checkout is missing\n' >&2
	exit 2
fi
actual=$(git -C "$nimble" rev-parse HEAD)
if [ "$actual" != "$expected" ]; then
	printf 'ble-source-check: NimBLE revision does not match platform.lock.yml\n' >&2
	exit 2
fi
printf '  ok   NimBLE revision matches platform.lock.yml\n'

for item in \
	'porting/npl/freertos/src/npl_os_freertos.c' \
	'porting/npl/freertos/src/nimble_port_freertos.c' \
	'nimble/host/include/host/ble_gatt.h' \
	'nimble/host/include/host/ble_l2cap.h' \
	'nimble/host/src/ble_l2cap_coc.c'; do
	if [ ! -f "$nimble/$item" ]; then
		printf 'ble-source-check: required NimBLE component is missing: %s\n' "$item" >&2
		exit 2
	fi
done

l2cap="$nimble/nimble/host/include/host/ble_l2cap.h"
for symbol in ble_l2cap_create_server ble_l2cap_send ble_l2cap_recv_ready; do
	if ! rg -q "[[:space:]]${symbol}\\(" "$l2cap"; then
		printf 'ble-source-check: required L2CAP CoC API is missing: %s\n' "$symbol" >&2
		exit 2
	fi
done
if ! rg -q 'BLE_L2CAP_EVENT_COC_(CONNECTED|DATA_RECEIVED)' "$l2cap"; then
	printf 'ble-source-check: required L2CAP CoC events are missing\n' >&2
	exit 2
fi

printf '  ok   upstream FreeRTOS NPL and host-task ports are present\n'
printf '  ok   GATT and credit-based L2CAP server/send/receive contracts are present\n'
printf 'RESULT: PINNED BLE HOST ACCEPTED; SDC init/dispatcher and board proof remain\n'
