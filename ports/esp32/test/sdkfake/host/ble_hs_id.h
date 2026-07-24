/* sdkfake host/ble_hs_id.h */
#ifndef SDKFAKE_HOST_BLE_HS_ID_H
#define SDKFAKE_HOST_BLE_HS_ID_H

#include "ble_hs.h"

int ble_hs_id_infer_auto(int privacy, uint8_t *own_addr_type);
int ble_hs_id_copy_addr(uint8_t addr_type, uint8_t *out, int *num);

extern int fake_hs_id_infer_rc;
extern uint8_t fake_hs_id_own_addr_type; /* what infer_auto writes */
extern int fake_hs_id_copy_rc;
extern uint8_t fake_hs_id_addr[6];       /* identity address, NimBLE LSB-first */

#endif
