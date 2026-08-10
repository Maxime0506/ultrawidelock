/*
 * Zephyr-shaped shim, not Zephyr code.
 *
 * The pinned dispatcher's header declares a struct holding the Zephyr HCI
 * driver receive callback. Nothing in this port instantiates or calls it: the
 * receive path is ble/nimble_sdc_transport.c. The typedef only has to exist,
 * so both of its parameter types stay incomplete and unusable by accident.
 */
#ifndef WOZ_HCI_COMPAT_DRIVERS_BLUETOOTH_H
#define WOZ_HCI_COMPAT_DRIVERS_BLUETOOTH_H

struct device;
struct net_buf;

typedef int (*bt_hci_recv_t)(const struct device *dev, struct net_buf *buf);

#endif /* WOZ_HCI_COMPAT_DRIVERS_BLUETOOTH_H */
