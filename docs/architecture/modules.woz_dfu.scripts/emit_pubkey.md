<!-- generated documentation — edit the source, not this file -->
# `modules/woz_dfu/scripts/emit_pubkey.py`

Emit the public half of the MCUboot signing key as a C array.

The application verifies a staged patch's header against this before writing it
to flash, so the key that signs images and the key that authorises updates are
the SAME key. That is the point: one secret to hold, one to rotate, and no way
for the two to drift apart.

Only the public half is emitted. The private key never enters the build output.

    emit_pubkey.py <signing-key.pem> <out.c>

<details><summary>Undocumented (1)</summary>

- `main`

</details>
