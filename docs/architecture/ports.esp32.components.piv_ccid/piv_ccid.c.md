<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/piv_ccid/piv_ccid.c`

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**depends on** [`ports/esp32/components/piv_ccid/include/piv_apdu.h`](../ports.esp32.components.piv_ccid.include/piv_apdu.h.md), [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](../ports.esp32.components.piv_ccid.include/piv_ccid.h.md)

## API

### `static uint32_t get_le32(const uint8_t *p)`
`ports/esp32/components/piv_ccid/piv_ccid.c:35`

Read a little-endian 32-bit unsigned integer from a 4-byte buffer.

**called by** `piv_ccid_process`

### `static void put_le32(uint8_t *p, uint32_t value)`
`ports/esp32/components/piv_ccid/piv_ccid.c:46`

Write a little-endian 32-bit unsigned integer into a 4-byte buffer.

**called by** `reply`

### `static uint8_t icc_status(const struct piv_ccid *ccid)`
`ports/esp32/components/piv_ccid/piv_ccid.c:58`

Encode card status byte reflecting current power state: CCID_STATUS_ICC_ACTIVE if powered, else
CCID_STATUS_ICC_INACTIVE.

**called by** `piv_ccid_process`, `slot_status`

### `void piv_ccid_init(struct piv_ccid *ccid, const struct piv_apdu_backend *backend, void *backend_ctx, bool pin_required)`
`ports/esp32/components/piv_ccid/piv_ccid.c:107`

Initialize PIV CCID protocol handler: clear struct, register APDU backend and context, set PIN
requirement flag.

<details><summary>Undocumented (3)</summary>

- `reply`
- `slot_status`
- `piv_ccid_process` — tested: rejections and parameters; slot and power

</details>
