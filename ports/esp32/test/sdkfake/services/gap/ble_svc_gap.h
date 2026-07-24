/* sdkfake services/gap/ble_svc_gap.h */
#ifndef SDKFAKE_BLE_SVC_GAP_H
#define SDKFAKE_BLE_SVC_GAP_H

void ble_svc_gap_init(void);
int ble_svc_gap_device_name_set(const char *name);
const char *ble_svc_gap_device_name(void);

extern int fake_svc_gap_name_set_rc;
extern char fake_svc_gap_name[32];

#endif
