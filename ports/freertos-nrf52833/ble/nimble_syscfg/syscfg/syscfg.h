/*
 * NimBLE configuration for this product.
 *
 * NimBLE resolves every setting through MYNEWT_VAL(), and upstream's
 * porting/nimble/include/syscfg/syscfg.h guards all 1500-odd defaults with
 * #ifndef. This header therefore only states the values that must differ, then
 * chains to upstream for the rest, so the vendor tree is neither copied nor
 * patched. Put this directory ahead of upstream's on the include path.
 *
 * Every role setting below has to agree with the controller features that
 * radio/radio_start_freertos.c links with sdc_support_*. Enabling a role here
 * that the controller does not support would make NimBLE issue commands the
 * controller rejects at runtime instead of failing at build time.
 */
#ifndef WOZ_FREERTOS_NIMBLE_SYSCFG_H
#define WOZ_FREERTOS_NIMBLE_SYSCFG_H

/*
 * Upstream defaults both of these to 1. The controller links neither
 * sdc_support_central nor sdc_support_scan, so they must be off.
 */
#define MYNEWT_VAL_BLE_ROLE_CENTRAL (0)
#define MYNEWT_VAL_BLE_ROLE_OBSERVER (0)

/* sdc_support_adv and sdc_support_peripheral are linked. */
#define MYNEWT_VAL_BLE_ROLE_BROADCASTER (1)
#define MYNEWT_VAL_BLE_ROLE_PERIPHERAL (1)

/*
 * Legacy advertising only. sdc_support_ext_adv is not linked, and leaving this
 * at zero also keeps upstream from raising the transport event buffers to 257
 * bytes for extended advertising reports this build can never receive.
 */
#define MYNEWT_VAL_BLE_EXT_ADV (0)

/* Must equal the controller's SDC_CFG_TYPE_PERIPHERAL_COUNT. */
#define MYNEWT_VAL_BLE_MAX_CONNECTIONS (1)

/*
 * Credit-based L2CAP is how Aliro moves its larger payloads. Upstream defaults
 * the channel count to zero, which compiles the whole CoC path out.
 */
#define MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM (1)

/*
 * LE Secure Connections only. Aliro's session establishment requires it, and
 * allowing the legacy pairing fallback would let a peer negotiate down.
 */
#define MYNEWT_VAL_BLE_SM_SC (1)
#define MYNEWT_VAL_BLE_SM_LEGACY (0)

/*
 * Bonding stays off until the port has a settings backend to persist keys
 * into. Turning it on without a store would accept pairings that silently do
 * not survive a reboot.
 */
#define MYNEWT_VAL_BLE_SM_BONDING (0)

/*
 * Privacy has no setting here to turn off: NimBLE selects it per call through
 * own_addr_type. The controller does not link sdc_support_le_privacy, so it
 * has no resolving list, and product code must therefore only ever pass
 * BLE_OWN_ADDR_PUBLIC or BLE_OWN_ADDR_RANDOM. Requesting either
 * BLE_OWN_ADDR_RPA_* type would fail at runtime, not at build time.
 */

#include_next <syscfg/syscfg.h>

#endif /* WOZ_FREERTOS_NIMBLE_SYSCFG_H */
