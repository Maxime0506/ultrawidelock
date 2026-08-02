<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_device.c`

Aliro initiator (User-Device) session machine: the implementation behind
aliro_device.h. Feeds one reader command at a time through
aliro_device_on_command, which parses AUTH0/AUTH1/EXCHANGE with the inverse
codec, runs the mirror of the reader's key schedule (ephemeral ECDH, the two
ECDSA transcripts, the session salt) and returns the sealed response. Owns the
two AES-256-GCM channels the device holds, the Access-Protocol channel and the
BleSK ranging channel, both split out of the same 160-byte key block, plus the
standard-path derivation factored EC-free so host tests can drive it with a
supplied shared secret.

**depends on** [`modules/woz_aliro/include/aliro_crypto.h`](../modules.woz_aliro.include/aliro_crypto.h.md), [`modules/woz_aliro/include/aliro_device.h`](../modules.woz_aliro.include/aliro_device.h.md), [`modules/woz_aliro/include/aliro_prim.h`](../modules.woz_aliro.include/aliro_prim.h.md), [`modules/woz_aliro/src/aliro_apdu.h`](aliro_apdu.h.md)

<details><summary>Undocumented (11)</summary>

- `aliro_dev_secchan_init` — tested: secchan
- `aliro_dev_secchan_open` — tested: secchan
- `aliro_dev_secchan_seal` — tested: secchan
- `aliro_dev_blesk_init` — tested: blesk channel; blesk salt
- `aliro_dev_ble_open` — tested: blesk channel; ranging channel after auth1
- `aliro_dev_ble_seal` — tested: blesk channel; ranging channel after auth1
- `aliro_dev_seal_cryptogram` — tested: cryptogram
- `aliro_device_derive_session` — tested: key schedule
- `aliro_device_init`
- `aliro_device_set_blesk_salt` — tested: blesk salt
- `aliro_device_on_command`

</details>
