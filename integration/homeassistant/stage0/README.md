# Home Assistant Stage 0 evidence

This directory is the evidence contract for the Home Assistant productization
work. It is deliberately separate from the normal firmware build and test
paths. Validate it with:

```bash
make ha-stage0 HA=1
```

The target opens no serial port, broker, or firmware image. `HA=1` is required
so this Home Assistant-specific work stays out of the default path. It does not
change firmware behavior or establish compatibility for images built without
`HA=1`.

## Current status

The synthetic fixture proves the existing bridge parser's current streaming and
access behavior. Every item in `captures/manifest.json` under
`required_hardware_cases` is pending. Stage 0 does not pass until those entries
are replaced with reviewed hardware evidence.

## Collecting hardware evidence

For each supported host setup, first identify the active VCOM1 interface by
observing the `aliro` shell. Record the vendor ID, product ID, interface number,
product text, and a local pseudonym such as `board_a`; never commit a raw USB
serial number or device path.

Capture a normal console and a PRETTY console. For each, record boot output and
the responses to `aliro version`, `aliro status`, `aliro frames on`, `aliro
frames`, `aliro range`, and `aliro frames off`. Capture one granted and one
denied access outcome, then unplug/reset the board and prove that the same
physical device is rediscovered and reopened.

The Home Assistant OS, Container, and Core records must each state whether the
port can be opened exclusively, used, disconnected, rediscovered, and reopened
after both a board reset and a Home Assistant restart. Do not infer one result
from another installation type.

## Sanitization rules

Fixtures must contain only console text needed to verify the contract. Replace
credential identifiers with `<redacted>`, remove peer addresses, hostnames,
paths, USB serial numbers, certificate material, and raw protocol frames. Do
not include complete unreviewed console logs.

Each complete capture needs an expected-observations JSON sidecar. The current
synthetic sidecar describes only observations the bridge can already parse;
future sidecars may add typed version, status, and compatibility-range
observations after their parser contract is implemented.

Unknown console versions remain unsupported for distance and access until a
reviewed capture and parser test establish compatibility.
