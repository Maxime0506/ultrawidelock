/*
 * Recording double for the NimBLE host configuration surface. Only the two
 * callbacks and the start entry point that ble/nimble_host_freertos.c touches;
 * the field names and prototypes mirror the pinned NimBLE header and are
 * asserted against it by scripts/freertos-ble-source-check.sh.
 */
#ifndef TEST_HOST_BLE_HS_H
#define TEST_HOST_BLE_HS_H

typedef void ble_hs_reset_fn(int reason);
typedef void ble_hs_sync_fn(void);

struct ble_hs_cfg {
	ble_hs_reset_fn *reset_cb;
	ble_hs_sync_fn *sync_cb;
};

extern struct ble_hs_cfg ble_hs_cfg;

void ble_hs_sched_start(void);

#endif /* TEST_HOST_BLE_HS_H */
