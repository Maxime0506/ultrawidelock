<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/pn532_apdu.h`

PN532-specific ISO 7816 APDU adaptation.
The Aliro stack (including the prebuilt library) negotiates sizes with the
User Device, but has no API for the reader controller's smaller local limit.
This adapter keeps that hardware constraint at the transport boundary.

**used by** [`modules/woz_nfc/src/pn532_apdu.c`](pn532_apdu.c.md), [`modules/woz_nfc/src/transport_pn532.cpp`](transport_pn532.cpp.md)

## API

### `struct woz_pn532_apdu_plan`
`modules/woz_nfc/src/pn532_apdu.h:38`

State machine for chunking an ISO 7816 APDU across multiple SPI frames: tracks input buffer,
payload offset and length, amount emitted, expected response length (Le), and mode flags
(extended-length, has-Le). Used by the PN532 driver to split oversized commands.
