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

<details><summary>Undocumented (4)</summary>

- `aliro_ble_central_parse_adv` — tested: adv
- `aliro_ble_central_adv_matches` — tested: adv
- `aliro_ble_central_parse_read_payload` — tested: blesk salt; read payload
- `aliro_ble_central_blesk_salt` — tested: blesk salt

</details>
