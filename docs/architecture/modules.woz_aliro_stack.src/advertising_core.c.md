<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/advertising_core.c`

**depends on** [`modules/woz_aliro_stack/src/advertising_core.h`](advertising_core.h.md)

## API

### `void woz_aliro_dynamic_tag_input(const uint8_t address_le[WOZ_ALIRO_BLE_ADDRESS_SIZE], uint32_t expiry_timestamp, uint8_t plaintext[WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE])`
`modules/woz_aliro_stack/src/advertising_core.c:5`

address_le uses Zephyr/bt_addr_t storage order. The generated AES input is
6 zero pad bytes || AdvA most-significant-byte first || expiry big-endian.

### `void woz_aliro_dynamic_tag_from_ciphertext(const uint8_t ciphertext[WOZ_ALIRO_DYNAMIC_TAG_INPUT_SIZE], uint8_t tag[WOZ_ALIRO_DYNAMIC_TAG_SIZE])`
`modules/woz_aliro_stack/src/advertising_core.c:19`

The dynamic tag is the seven most-significant AES ciphertext octets.
