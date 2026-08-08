<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_ble_central.h`

Device-side (User-Device) BLE transport interface: the central/client mirror of
aliro_ble.h. Where the reader advertises 0xFFF2, serves the GATT characteristics
and runs an L2CAP CoC server, the initiator scans, connects, reads the reader's
SPSM/versions, writes its selected version and opens a CoC client to that SPSM.
The platform-free half (advert + READ-payload decoding, BleSK salt assembly)
lives in aliro_ble_central.c and is host-testable; the NimBLE backend for the
transport calls sits in ports/esp32, so a Zephyr bt_gap_*/bt_l2cap_* backend
can be written behind this same header.

**depends on** [`modules/woz_aliro/include/aliro_advtag.h`](aliro_advtag.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_ble_central.c`](../modules.woz_aliro.src/aliro_ble_central.c.md)  ·  **discussed in** [`CHANGELOG.md`](../../../CHANGELOG.md)

## API

### `struct aliro_ble_central_adv`
`modules/woz_aliro/include/aliro_ble_central.h:38`

Reader identity recovered from one 0xFFF2 service-data advert. Field order is
the inverse of build_aliro_svc_data (aliro_ble.c:532).

### `struct aliro_ble_central_peer`
`modules/woz_aliro/include/aliro_ble_central.h:49`

What the peer publishes on the reader-SPSM characteristic
(D3B5A130-9E23-4B3A-8BE4-6B1EE5F980A3), read before the CoC opens.

### `struct aliro_ble_central_callbacks`
`modules/woz_aliro/include/aliro_ble_central.h:90`

Callbacks for BLE central transport: on_ready when CoC opens and peer facts are known, on_data
for each inbound SDU, on_closed when link or CoC drops.

### `struct aliro_ble_central_config`
`modules/woz_aliro/include/aliro_ble_central.h:104`

Configuration for BLE central transport: reader_id to match by truncated group/sub ID in adverts,
selected_version for device-version characteristic, and callbacks.
