# Installed C API

`include/openaliro/openaliro.h` is the all-in-one package umbrella. The real
role declarations stay with their portable implementation owners:

| Include | Canonical source |
|---|---|
| `<openaliro/reader.h>` | `modules/woz_aliro/include/openaliro/reader.h` |
| `<openaliro/device.h>` | `modules/woz_aliro/include/openaliro/device.h` |
| `<openaliro/tlv.h>` | `modules/woz_aliro/include/openaliro/tlv.h` |
| `<openaliro/uwb.h>` | `modules/woz_uwb/include/openaliro/uwb.h` |
| `<openaliro/woz_hal.h>` | `modules/woz_port/include/openaliro/woz_hal.h` |

This keeps module ownership intact while giving source builds and installed
packages the same `<openaliro/...>` include spelling. The former flat role
headers were removed, and the SDK test rejects their reintroduction.

The package installs these entry points plus only the lower-level headers they
include. That exact installed set is ratcheted by `make sdk-check`; adding or
removing a package header requires an explicit package and test update.

Implementation remains in `modules/*/src/`; this directory stays headers-only.
