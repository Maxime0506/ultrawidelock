// ESP-IDF bring-up for the Aliro reader's NimBLE transport.
//
// The portable half -- the GATT service, advertising, and the L2CAP CoC server -- is
// modules/woz_aliro/src/aliro_ble_nimble.c and is shared with the standalone FreeRTOS
// port. What stays here is the part that names ESP-IDF: NVS for NimBLE's key store,
// esp-nimble's nimble_port_init(), and the FreeRTOS host task esp-nimble starts for us.
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"

#include "aliro_ble.h"

static const char *TAG = "aliro_ble_esp";

// Runs the NimBLE host until stopped. nimble_port_run() returns only on
// nimble_port_stop(); param is unused.
static void host_task(void *param)
{
	(void)param;
	nimble_port_run();
	nimble_port_freertos_deinit();
}

// Bring up the Aliro BLE service as a standalone NimBLE host: init NVS, init the NimBLE
// port, register the service through the shared backend, and start the host task.
// Returns -1 on any NimBLE port or registration failure, 0 on success. NVS init errors
// other than the handled no-free-pages/new-version cases abort via ESP_ERROR_CHECK.
int aliro_ble_start(const struct aliro_ble_config *cfg)
{
	/* Before NVS and the port, so a bad config costs nothing and this returns
	 * with the platform exactly as it was found. */
	if (aliro_ble_prepare(cfg) != 0) {
		return -1;
	}

	esp_err_t err = nvs_flash_init(); /* NimBLE stores bonding/keys in NVS */
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	err = nimble_port_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "nimble_port_init rc=%d", err);
		return -1;
	}

	ble_hs_cfg.sync_cb = aliro_ble_host_sync;
	ble_hs_cfg.reset_cb = aliro_ble_host_reset;

	if (aliro_ble_register_gatt() != 0) {
		ESP_LOGE(TAG, "aliro_ble_register_gatt failed");
		return -1;
	}

	nimble_port_freertos_init(host_task);
	return 0;
}
