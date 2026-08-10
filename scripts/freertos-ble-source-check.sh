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

# ble/nimble_host_freertos.c calls exactly these, and the recording doubles in
# tests/ports/freertos-nrf52833/fake reproduce these prototypes.
port_h="$nimble/porting/nimble/include/nimble/nimble_port.h"
hs_h="$nimble/nimble/host/include/host/ble_hs.h"
if ! rg -q '^void nimble_port_init\(void\);' "$port_h" || \
	! rg -q '^void nimble_port_run\(void\);' "$port_h"; then
	printf 'ble-source-check: NimBLE porting-layer init contract changed\n' >&2
	exit 2
fi
if ! rg -q '^void ble_hs_sched_start\(void\);' "$hs_h" || \
	! rg -Fq 'ble_hs_reset_fn *reset_cb;' "$hs_h" || \
	! rg -Fq 'ble_hs_sync_fn *sync_cb;' "$hs_h" || \
	! rg -q 'typedef void ble_hs_reset_fn\(int reason\);' "$hs_h" || \
	! rg -q 'typedef void ble_hs_sync_fn\(void\);' "$hs_h"; then
	printf 'ble-source-check: NimBLE host start or lifecycle callback contract changed\n' >&2
	exit 2
fi

# nimble_port_init() is the only thing that initializes the transport, and the
# transport half is this port's own ble_transport_ll_init. If upstream ever
# stops calling it there, the host would come up against a dead controller.
if ! rg -Fq 'ble_transport_ll_init();' "$nimble/porting/nimble/src/nimble_port.c"; then
	printf 'ble-source-check: nimble_port_init no longer initializes the transport\n' >&2
	exit 2
fi
printf '  ok   host start, lifecycle callbacks, and transport init match the port\n'

# The port is otherwise static-only. These are the upstream allocations that
# force a FreeRTOS heap, and the count is asserted so a version bump that adds
# more has to be re-costed rather than silently growing the heap.
npl="$nimble/porting/npl/freertos/src/npl_os_freertos.c"
npl_h="$nimble/porting/npl/freertos/include/nimble/nimble_npl_os.h"
dynamic=$(rg -c 'xSemaphoreCreateRecursiveMutex\(|xSemaphoreCreateCounting\(|xTimerCreate\(' "$npl" || true)
if [ "$dynamic" != "3" ]; then
	printf 'ble-source-check: NimBLE NPL dynamic allocation sites changed (%s, expected 3)\n' \
		"$dynamic" >&2
	exit 2
fi
if ! rg -Fq 'xQueueCreate(32, sizeof(struct ble_npl_eventq *))' "$npl_h"; then
	printf 'ble-source-check: NimBLE NPL event-queue allocation changed\n' >&2
	exit 2
fi
printf '  ok   NPL heap use is the known event queue, mutexes, semaphore, and timers\n'

printf 'RESULT: PINNED BLE HOST ACCEPTED; board proof remains\n'
