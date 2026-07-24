/* matterfake host/ble_hs.h — shadows the sdkfake NimBLE header: app_main only
 * needs the service-definition type and the host-sync probe. */
#ifndef MATTERFAKE_HOST_BLE_HS_H
#define MATTERFAKE_HOST_BLE_HS_H

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gatt_svc_def {
	int type;
};

int ble_hs_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* MATTERFAKE_HOST_BLE_HS_H */
