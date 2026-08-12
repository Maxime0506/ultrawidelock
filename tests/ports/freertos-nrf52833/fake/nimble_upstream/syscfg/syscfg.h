/*
 * Stand-in for upstream's porting/nimble/include/syscfg/syscfg.h, which the
 * port's own syscfg header chains to with #include_next. Only the settings the
 * host tests actually read, each guarded exactly the way upstream guards its
 * own defaults, so the port header's overrides win here for the same reason
 * they win on target. Upstream's real defaults are asserted separately by
 * scripts/freertos-ble-source-check.sh.
 */
#ifndef TEST_NIMBLE_UPSTREAM_SYSCFG_H
#define TEST_NIMBLE_UPSTREAM_SYSCFG_H

#define MYNEWT_VAL(_name) MYNEWT_VAL_##_name

#ifndef MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE
#define MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE (70)
#endif

#ifndef MYNEWT_VAL_BLE_ROLE_CENTRAL
#define MYNEWT_VAL_BLE_ROLE_CENTRAL (1)
#endif

#ifndef MYNEWT_VAL_BLE_ROLE_OBSERVER
#define MYNEWT_VAL_BLE_ROLE_OBSERVER (1)
#endif

#ifndef MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM
#define MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM (0)
#endif

#ifndef MYNEWT_VAL_BLE_SM_SC
#define MYNEWT_VAL_BLE_SM_SC (0)
#endif

#ifndef MYNEWT_VAL_BLE_SM_LEGACY
#define MYNEWT_VAL_BLE_SM_LEGACY (1)
#endif

#endif /* TEST_NIMBLE_UPSTREAM_SYSCFG_H */
