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

<details><summary>Undocumented (4)</summary>

- `on_curve`
- `add`
- `mul`
- `main`

</details>
