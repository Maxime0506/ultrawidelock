<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/scripts/emit_pubkey.py`

Emit the public half of the MCUboot signing key as a C array.

The application verifies a staged patch's header against this before writing it
to flash, so the key that signs images and the key that authorises updates are
the SAME key. That is the point: one secret to hold, one to rotate, and no way
for the two to drift apart.

Only the public half is emitted. The private key never enters the build output.

    emit_pubkey.py <signing-key.pem> <out.c>

## API

### `main()`
`modules/woz_dfu/scripts/emit_pubkey.py:18`

Read an ECDSA P-256 private key from PEM format, extract its X9.62 uncompressed point public key, and emit it as a C header file for the MCUboot image-signing key.
