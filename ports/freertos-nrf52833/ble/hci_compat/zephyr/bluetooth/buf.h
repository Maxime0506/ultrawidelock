/*
 * Zephyr-shaped shim, not Zephyr code.
 *
 * The pinned dispatcher sizes its single static Command Complete / Command
 * Status staging buffer with BT_BUF_EVT_RX_SIZE. The port has no net_buf
 * layer, so there is no headroom to reserve and the bound is simply the
 * largest HCI event packet the controller can produce.
 */
#ifndef WOZ_HCI_COMPAT_BLUETOOTH_BUF_H
#define WOZ_HCI_COMPAT_BLUETOOTH_BUF_H

#include <sdc_hci.h>

#define BT_BUF_EVT_RX_SIZE HCI_EVENT_PACKET_MAX_SIZE

#endif /* WOZ_HCI_COMPAT_BLUETOOTH_BUF_H */
