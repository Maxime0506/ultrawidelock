<!-- generated documentation — edit the source, not this file -->
# `web-flasher/check_codes.py`

Verify that the Matter commissioning codes shown on the browser flasher page still match the firmware it flashes. Recomputes the QR payload and manual pairing code from the CHIP test-setup constants, checks the page against them, and can regenerate the inline QR image.

**discussed in** [`web-flasher/README.md`](../../../web-flasher/README.md)

## API

### `check_digit(digits: str) -> str`
`web-flasher/check_codes.py:112`

Return the Verhoeff check digit for a decimal string, the last digit of a Matter manual pairing code.

**called by** `manual_code`

### `base38(data: bytes) -> str`
`web-flasher/check_codes.py:120`

Encode bytes in Matter's base-38 alphabet: three bytes become five characters, a two-byte tail four, a one-byte tail two.

**called by** `qr_payload`

### `qr_payload(vid=None, pid=None, rendezvous=None, disc=None, passcode=None) -> str`
`web-flasher/check_codes.py:132`

Return the MT: onboarding payload string, packing the fields least-significant-bit first the way CHIP's QRCodeSetupPayloadGenerator does. The arguments exist so self_test() can run CHIP's own vector through this; they resolve at call time, not at definition, so the module constants stay the single source of truth.

**called by** `main`  ·  **calls** `base38`

### `manual_code(disc=None, passcode=None) -> str`
`web-flasher/check_codes.py:155`

Return the 11-digit manual pairing code: one digit of discriminator, five of passcode plus discriminator, four of passcode, and a check digit. Arguments resolve at call time; see qr_payload.

**called by** `main`  ·  **calls** `check_digit`

### `grouped(code: str) -> str`
`web-flasher/check_codes.py:176`

Return the manual pairing code in the 4-3-4 grouping Apple Home and the Matter spec print.

**called by** `main`

### `qr_svg(payload: str) -> str`
`web-flasher/check_codes.py:181`

Render the payload as an inline SVG QR code, one path with a horizontal run per dark segment. Needs segno, so this runs only when regenerating.

**called by** `main`

### `upstream_layer(payload_ok: list[str]) -> None`
`web-flasher/check_codes.py:208`

Re-read the four upstream constants from an esp-matter checkout when ESP_MATTER_PATH names one, and record a pass, a skip or a failure.

**called by** `main`

### `main() -> int`
`web-flasher/check_codes.py:231`

Check the flasher page's commissioning codes against the constants, or print a fresh QR block with --svg. Returns 1 on drift.

**calls** `grouped`, `manual_code`, `qr_payload`, `qr_svg`, `upstream_layer`
