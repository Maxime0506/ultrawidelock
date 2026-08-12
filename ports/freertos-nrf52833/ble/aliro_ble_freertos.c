// Standalone-FreeRTOS bring-up for the Aliro reader's NimBLE transport.
//
// The portable half -- the GATT service, advertising, and the L2CAP CoC server -- is
// modules/ultrawidelock_cred/src/ultrawidelock_ble_nimble.c, shared with the ESP32 port. What is
// here is the part that names this platform, and it is smaller than the ESP-IDF equivalent because
// the port already owns NimBLE: woz_freertos_nimble_host_start() does nimble_port_init() and the
// host task, so this only has to hand it the two hooks and make sure the key-value store is up
// first.
//
// BONDING. The ESP-IDF path initialises NVS here because esp-nimble persists SM keys in
// it. This port does not, because MYNEWT_VAL_BLE_SM_BONDING is 0 in
// ble/nimble_syscfg/syscfg/syscfg.h: Aliro carries its own credentials through the
// attestation exchange on the CoC, so SM bonding is not the mechanism and no NimBLE store
// backend is linked. Turning bonding on means building ble_store_config.c against
// woz_freertos_kv and revisiting this file -- it is not a syscfg edit on its own.
//
// The key-value store is deliberately NOT initialised here. ultrawidelock_reader_start() loads
// the provisioned identity out of it before it ever reaches this file, so by the time
// this runs the store must already be up; doing it here would be too late to matter and
// would read as though it were not. The application owns that call.
#include <stddef.h>

#include "ultrawidelock_ble.h"

#include "woz_freertos_nimble_host.h"
#include "woz_freertos_platform.h"

#define TAG "ultrawidelock_ble"

/* Nothing is held between the two halves: ultrawidelock_ble_prepare() below copies the config
 * into the backend's own statics, so the caller may pass a stack local, and by the time
 * this hook runs there is nothing left to point at. */
static const struct woz_freertos_nimble_host_hooks k_hooks = {
	.register_services = ultrawidelock_ble_register_gatt,
	.on_sync = ultrawidelock_ble_host_sync,
};

// Bring up the Aliro BLE reader on this port: install the hooks, then start the
// controller and host.
//
// cfg is copied, so the caller may pass a stack local and let it go. Returns 0 on success,
// or a negative woz_freertos_nimble_host_stage on failure; -1 if cfg is NULL.
//
// Returning 0 means the host task is running and the sync sequence is queued. Advertising
// begins later, from ultrawidelock_ble_host_sync() on the host task.
int ultrawidelock_ble_start(const struct ultrawidelock_ble_config *cfg)
{
	int rc;

	/* Before the host, so a bad config costs nothing and this returns with the
	 * radio exactly as it was found. */
	if (ultrawidelock_ble_prepare(cfg) != 0) {
		return -1;
	}

	woz_freertos_nimble_host_set_hooks(&k_hooks);

	rc = woz_freertos_nimble_host_start();
	if (rc != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, "host start failed at stage %d",
				 -rc);
		return rc;
	}

	woz_freertos_log(WOZ_FREERTOS_LOG_INFO, TAG,
			 "Aliro reader up; advertising once the host syncs (SPSM 0x%04x)",
			 (unsigned)ultrawidelock_ble_spsm());
	return 0;
}
