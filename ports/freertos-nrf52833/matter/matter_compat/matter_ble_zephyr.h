/*
 * The commissioning layer includes "matter_ble_zephyr.h" and calls
 * matter_ble_set_msg_handler(), matter_ble_send(), matter_ble_discriminator()
 * and the rest. Those names are the transport's CONTRACT, not Zephyr's: this
 * port implements every one of them in matter_ble_freertos.c, against NimBLE.
 *
 * So the only Zephyr thing left is the file name, and this redirects it. The
 * two headers were deliberately kept identical in their declarations for
 * exactly this reason -- the layer above should not know or care which BLE host
 * is underneath it.
 *
 * If the shared layer is ever moved to modules/ this file disappears and the
 * include becomes "matter_ble.h" on both sides.
 */
#ifndef WOZ_MATTER_COMPAT_MATTER_BLE_ZEPHYR_H
#define WOZ_MATTER_COMPAT_MATTER_BLE_ZEPHYR_H

#include "matter_ble_freertos.h"

#endif /* WOZ_MATTER_COMPAT_MATTER_BLE_ZEPHYR_H */
