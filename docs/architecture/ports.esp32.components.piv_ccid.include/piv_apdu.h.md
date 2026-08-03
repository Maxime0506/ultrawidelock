<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/piv_ccid/include/piv_apdu.h`

*No module docstring. First commit: "piv: add ESP32-S3 CCID bench transport".*

**used by** [`ports/esp32/components/piv_ccid/include/piv_ccid.h`](piv_ccid.h.md), [`ports/esp32/components/piv_ccid/include/piv_identity.h`](piv_identity.h.md), [`ports/esp32/components/piv_ccid/piv_apdu.c`](../ports.esp32.components.piv_ccid/piv_apdu.c.md), [`ports/esp32/components/piv_ccid/piv_ccid.c`](../ports.esp32.components.piv_ccid/piv_ccid.c.md)

## API

### `struct piv_apdu_backend`
`ports/esp32/components/piv_ccid/include/piv_apdu.h:32`

Platform operations for the persistent PIV identity.
PIN callbacks return 0 on success, 1 on a wrong PIN, -2 when the PIN is
blocked or has not been provisioned, and -1 on an internal failure. The
retry count is returned for ISO 7816 status 63Cx.
get_certificate selects slot 9A or 9D using key_ref. sign_hash receives the
off-card SHA-256 digest required by PIV GENERAL AUTHENTICATE. derive_shared
performs the P-256 ECDH primitive for slot 9D.

### `struct piv_apdu`
`ports/esp32/components/piv_ccid/include/piv_apdu.h:57`

PIV APDU command handler state: tracks APDU selection, PIN verification status, and buffered
response for chunked transmission.
