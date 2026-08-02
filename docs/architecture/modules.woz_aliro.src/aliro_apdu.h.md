<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_apdu.h`

APDU framing and parsing for the Aliro Access Protocol: builds outbound command APDUs via a
TLV writer and parses the AUTH0/AUTH1 response APDUs exchanged during the reader-device
handshake.

**used by** [`modules/woz_aliro/include/aliro_device_apdu.h`](../modules.woz_aliro.include/aliro_device_apdu.h.md), [`modules/woz_aliro/src/aliro_apdu.c`](aliro_apdu.c.md), [`modules/woz_aliro/src/aliro_device.c`](aliro_device.c.md), [`modules/woz_aliro/src/aliro_reader.c`](aliro_reader.c.md)

## API

### `struct aliro_tlv_w`
`modules/woz_aliro/src/aliro_apdu.h:61`

---- BER-TLV writer ----

### `struct aliro_auth0_response`
`modules/woz_aliro/src/aliro_apdu.h:114`

Fields parsed from an AUTH0Response APDU: the device's mandatory ephemeral
public key, plus the optional cryptogram sent when the device recognises the
reader and offers the fast path.

### `struct aliro_auth1_response`
`modules/woz_aliro/src/aliro_apdu.h:124`

Fields parsed from an AUTH1Response APDU: the device's mandatory signature
over the transcript, plus the device public key and signaling bitmap it sends
when the standard (non-fast) path is taken.
