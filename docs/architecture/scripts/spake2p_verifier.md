<!-- generated documentation — edit the source, not this file -->
# `scripts/spake2p_verifier.py`

Derive a SPAKE2+ verifier (w0 and L) for a Matter setup passcode.

A Matter device never stores its setup passcode. It stores the verifier, which
is what PBKDF2 over the passcode yields plus one scalar multiplication:

    w0s || w1s = PBKDF2-HMAC-SHA256(passcode as LE uint32, salt, iterations, 80)
    w0 = w0s mod n     w1 = w1s mod n     L = w1 * G

Someone who reads the flash gets w0 and L, which are enough to VERIFY a
commissioner that knows the passcode and not enough to impersonate one. That is
the whole point of the augmented form, and the reason this runs here rather than
on the device: L needs a base-point multiply, which is not one of the four
operations nrf_oberon exposes to the reader.

Usage:

    scripts/spake2p_verifier.py                     # CHIP's test pairing
    scripts/spake2p_verifier.py --passcode 12345678 --salt-b64 <...>

The output goes into CONFIG_ALIRO_MATTER_SPAKE2P_VERIFIER and friends
(ports/dwm3001cdk/app/Kconfig). Print nothing anywhere it will be logged: the
verifier is not a secret in the way the passcode is, but it identifies the
device and there is no reason to scatter it.

**discussed in** [`ports/dwm3001cdk/README.md`](../../../ports/dwm3001cdk/README.md)

## API

### `derive(passcode, salt, iterations)`
`scripts/spake2p_verifier.py:76`

The 97-byte verifier: w0 (32) then L (65, uncompressed).

**called by** `from_config`, `main`  ·  **calls** `mul`, `on_curve`

### `manual_code(discriminator, passcode)`
`scripts/spake2p_verifier.py:120`

The 11-digit manual pairing code, short form (no vendor/product id).

Matter core 5.1.4.1. The digits are the discriminator's top 2 bits, then
its next 2 bits packed above the passcode's low 14, then the passcode's
high 13, then a check digit -- so BOTH numbers are recoverable from the
code, which is why a commissioner needs nothing else to find the device.

**called by** `from_config`, `main`

### `read_config(path)`
`scripts/spake2p_verifier.py:139`

The five symbols the setup code needs, out of a Zephyr .config.

**called by** `from_config`

### `from_config(path)`
`scripts/spake2p_verifier.py:167`

Print the setup code for an image ALREADY BUILT, and prove it first.

The device stores a verifier and never the passcode, so nothing on the
board can print this and nothing on the board can notice the two drifting
apart. Here they can be checked against each other: re-derive the verifier
from the passcode symbol and compare. A mismatch means the code below would
be entered, accepted by the phone, and fail at Pake3 looking exactly like a
typo -- so it is an error, not a warning.

**called by** `main`  ·  **calls** `derive`, `manual_code`, `read_config`

<details><summary>Undocumented (4)</summary>

- `on_curve`
- `add`
- `mul`
- `main`

</details>
