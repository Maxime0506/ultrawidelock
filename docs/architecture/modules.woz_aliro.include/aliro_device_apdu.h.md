<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_device_apdu.h`

Device (User-Device) side of the Aliro Access-Protocol wire codec: the inverse
of aliro_apdu.c. Where aliro_apdu builds reader commands and parses device
responses, this parses the reader's AUTH0/AUTH1/EXCHANGE commands and builds
the device's AUTH0/AUTH1/EXCHANGE responses. Pure byte manipulation, no crypto
and no platform dependency, so it is host-KAT verifiable against the reader's
own builders/parsers (round-trip) and the recovered layouts.

**depends on** [`modules/woz_aliro/src/aliro_apdu.h`](../modules.woz_aliro.src/aliro_apdu.h.md)  ·  **used by** [`modules/woz_aliro/include/aliro_device.h`](aliro_device.h.md), [`modules/woz_aliro/src/aliro_device_apdu.c`](../modules.woz_aliro.src/aliro_device_apdu.c.md)

## API

### `struct aliro_auth0_command`
`modules/woz_aliro/include/aliro_device_apdu.h:32`

Fields parsed from an AUTH0Command TLV. All are mandatory on the wire.

### `struct aliro_auth1_command`
`modules/woz_aliro/include/aliro_device_apdu.h:42`

Fields parsed from an AUTH1Command TLV.

### `struct aliro_exchange_command`
`modules/woz_aliro/include/aliro_device_apdu.h:48`

Fields parsed from the DECRYPTED EXCHANGE command plaintext.
