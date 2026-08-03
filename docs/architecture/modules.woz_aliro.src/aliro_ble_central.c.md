<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_ble_central.c`

Platform-free half of the device-side BLE transport declared in
aliro_ble_central.h: decodes the reader's 0xFFF2 service-data advert, decodes
the reader-SPSM GATT READ payload (SPSM, supported protocol versions, feature
mask), and assembles the BleSK salt from the version list the reader actually
published rather than from a compiled-in constant. No BLE stack calls and no
allocation, so it builds on the host and is checked byte for byte against the
reader's own emitters.

**depends on** [`modules/woz_aliro/include/aliro_ble_central.h`](../modules.woz_aliro.include/aliro_ble_central.h.md)

## API

### `int aliro_ble_central_parse_adv(const uint8_t *svc_data, size_t len, struct aliro_ble_central_adv *out)`
`modules/woz_aliro/src/aliro_ble_central.c:25`

Parse a 26-byte Aliro 0xFFF2 service data payload into the struct: verify the service UUID and
extract flags, TX power, group/sub IDs, expiry, and tag.

### `int aliro_ble_central_adv_matches(const struct aliro_ble_central_adv *adv, const uint8_t reader_id[32])`
`modules/woz_aliro/src/aliro_ble_central.c:53`

Return true if the advertisement group_id and sub_id fields match the first 8 and last 2 bytes of
the given 32-byte reader_id, false otherwise.

### `int aliro_ble_central_parse_read_payload(const uint8_t *payload, size_t len, struct aliro_ble_central_peer *out)`
`modules/woz_aliro/src/aliro_ble_central.c:67`

Parse a BLE central read response payload containing SPSM, version list, and features; return 0
on success or -1 if the payload is malformed or truncated.

### `int aliro_ble_central_blesk_salt(const struct aliro_ble_central_peer *peer, uint16_t selected, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_aliro/src/aliro_ble_central.c:110`

Serialize all BLE versions from the peer plus the selected version as big-endian 2-byte pairs
into the output buffer, and return 0 on success or -1 if output is too small or the peer is
invalid.
