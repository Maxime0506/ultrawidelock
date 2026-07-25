<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/ble_timeout.c`

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](ble_timeout.h.md)

## API

### `int woz_aliro_ble_timeout_classify(const uint8_t *data, size_t data_length, enum woz_aliro_ble_timeout_message *kind)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:54`

Classify one complete, unencrypted Aliro BLE message.

**calls** `classify_attribute`

<details><summary>Undocumented (7)</summary>

- `classify_attribute`
- `has_response_timeout`
- `is_allowed_reply`
- `collision_replaces_pending`
- `set_pending`
- `clear_pending`
- `woz_aliro_ble_timeout_observe` — tested: aliro ble

</details>
