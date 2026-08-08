<!-- generated documentation — edit the source, not this file -->
# `firmware/src/dfu_ble_zephyr.c`

@file
@brief The over-the-air update channel: a second L2CAP CoC, and the button
that opens it.
WHY NOT mcumgr. SMP over Bluetooth was built and measured first. It costs
3,717 B of RAM on an image that had 7,448 B left, and its permission model
defaults to demanding a paired, authenticated link whenever BT_SMP is on --
which it is here, pulled in by L2CAP CoC. This reader must never ask a phone
to pair, because the walk-up unlock depends on it not asking. Setting the
permission to open instead hands an unauthenticated peer a write path into
flash, and mcumgr's OS group would hand it an unauthenticated reset command
as well. A lock anyone in radio range can reboot in a loop is a real attack.
So the patch rides the CoC transport this board already has, on its own PSM,
and authorization is a WINDOW rather than a handshake.
WHY A WINDOW IS ENOUGH. The gate is a denial-of-service control, not an
integrity one. The patch header is signed and the application checks it
(modules/woz_dfu/src/dfu_receiver.c), and underneath that MCUboot re-verifies
the P-256 signature of the patched RESULT before booting it. No peer can
install code no matter what reaches this channel. What a closed channel
prevents is a stranger spending the flash's erase cycles and the owner's
uptime.

**discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `static struct net_buf *dfu_alloc_buf(struct bt_l2cap_chan *chan)`
`firmware/src/dfu_ble_zephyr.c:68`

DFU RX buffer allocation callback. Allocates a network buffer from the DFU RX pool with no wait,
or returns NULL if the pool is exhausted.

### `static int dfu_recv(struct bt_l2cap_chan *chan, struct net_buf *buf)`
`firmware/src/dfu_ble_zephyr.c:79`

L2CAP channel RX callback for DFU firmware updates. Processes received data through
woz_dfu_rx_frame, allocates a TX buffer for any response, and sends it back over the L2CAP
channel. Returns 0 on all paths.

### `static void dfu_connected(struct bt_l2cap_chan *chan)`
`firmware/src/dfu_ble_zephyr.c:107`

L2CAP channel connected callback for DFU firmware updates. Logs that the update channel is open.

### `static void dfu_disconnected(struct bt_l2cap_chan *chan)`
`firmware/src/dfu_ble_zephyr.c:118`

L2CAP channel disconnected callback for DFU firmware updates. Marks the channel as no longer in
use and resets any staged DFU bytes, so the next attempt starts clean if the connection drops
mid-transfer.

### `static int dfu_accept(struct bt_conn *conn, struct bt_l2cap_server *server, struct bt_l2cap_chan **chan)`
`firmware/src/dfu_ble_zephyr.c:141`

L2CAP channel accept callback for DFU firmware updates. Returns -EACCES if no DFU window is open
(the gate), -ENOMEM if a channel is already in use, or 0 on success with the channel configured
and its pointer assigned to the caller's channel reference.

### `static ssize_t dfu_gatt_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)`
`firmware/src/dfu_ble_zephyr.c:218`

GATT write callback for DFU firmware updates. Rejects writes with a nonzero offset, processes the
frame through woz_dfu_rx_frame, and notifies the client of any response. Returns the number of
bytes consumed on success.

### `static void button_work_fn(struct k_work *work)`
`firmware/src/dfu_ble_zephyr.c:263`

Work item handler for the DFU button press. Opens a DFU window for the duration specified by
CONFIG_WOZ_DFU_WINDOW_MS.

### `static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)`
`firmware/src/dfu_ble_zephyr.c:274`

GPIO interrupt handler for the DFU button. Submits the button work item to be processed off the
ISR, since opening the DFU window logs and touches a work queue.

### `int dfu_ble_start(void)`
`firmware/src/dfu_ble_zephyr.c:290`

Initialize the DFU update channel. Registers the L2CAP server, configures the optional button for
software window control if available, and logs readiness or warnings if the button is not ready.
Returns 0 on success or if button config fails gracefully (software-only mode), negative on L2CAP
server registration failure.
