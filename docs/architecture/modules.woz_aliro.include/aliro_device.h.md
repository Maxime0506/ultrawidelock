<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/include/aliro_device.h`

Aliro initiator (User-Device) session layer: the device-side counterpart of
aliro_reader.c. Drives the credential-auth handshake from the phone/fob role —
parses the reader's AUTH0/AUTH1/EXCHANGE commands, runs the mirror-image key
schedule (ECDH, the two ECDSA transcripts, the §8.3.1.13 salt), and produces
the sealed responses. The result is the same 32-byte URSK the reader derives.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_device_apdu.h`](aliro_device_apdu.h.md)  ·  **used by** [`modules/woz_aliro/src/aliro_device.c`](../modules.woz_aliro.src/aliro_device.c.md)

## API

### `struct aliro_dev_secchan`
`modules/woz_aliro/include/aliro_device.h:34`

Device view of an Aliro AES-256-GCM channel. The reader's aliro_secchan seals
on direction 0 and opens on direction 1; the device is the mirror — it OPENS
reader->device traffic (direction 0, key s0) and SEALS device->reader traffic
(direction 1, key s1). Both per-direction counters start at 1 (§8.3.1.13).

<details><summary>Undocumented (1)</summary>

- `aliro_device`

</details>
