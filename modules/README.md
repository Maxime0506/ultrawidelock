# Modules

This tree contains both platform-neutral cores and target integration modules.
Portability is stated per module below; not every directory builds unchanged for every
target. The nRF-specific Aliro replacement lives here because it implements a module
API, while target-specific application and backend code remains under
[`ports/`](../ports). The `woz_` prefix is this project's namespace, distinguishing it
from the fetched Nordic add-on and the vendored Qorvo driver; the `aliro_*` names are the
protocol-facing APIs those modules export.

Module roles, consumers, and direct dependencies:

| Module | What it is | Built by | Depends on |
|---|---|---|---|
| [`woz_port/`](woz_port/) | Platform contract: `woz_port.h` and `woz_log.h`. Start here when porting. | nRF5340, ESP32-S3/C5, host | platform branch selected in its headers |
| [`woz_uwb/`](woz_uwb/) | UWB engine: DW3000 glue, FiRa MAC, CCC key ladder and STS, DS-TWR responder, and M1-M4 codec. | nRF5340, ESP32-S3/C5, host | `woz_port`, `deps/dw3000` |
| [`woz_aliro/`](woz_aliro/) | Portable C reader used by the ESP32 apps: key schedule, secure channels, wire codec, provisioning, ranging glue, RSSI gate, and approach controller. | ESP32-S3/C5, host | `woz_port`, `woz_uwb` for ranging |
| [`woz_aliro_stack/`](woz_aliro_stack/) | Default nRF Aliro API implementation; `ALIRO_SOURCE=0` selects the legacy Nordic binary fallback. | nRF5340 default, host protocol tests | Zephyr/NCS integration; portable protocol files are host-tested |
| [`woz_nfc/`](woz_nfc/) | nRF NFC transport seam with RFAL, PN532, and no-reader backends. | nRF5340; PN532 driver/APDU files also build on host | Zephyr/NCS and the selected reader backend |
| [`woz_aliro_ecp/`](woz_aliro_ecp/) | NFC ECP emitter for Express Mode tap, using Nordic-licensed vendor code. | nRF5340 only | Zephyr/NCS |

Build integration: the nRF build registers its Zephyr modules through
`ZEPHYR_EXTRA_MODULES` and enables `woz_aliro_stack` by default; the remaining
modules use their `CONFIG_WOZ_*` gates. The ESP-IDF components under
[`ports/esp32/components/`](../ports/esp32/components) select the portable UWB and C
reader sources they consume. The host suite in [`tests/host`](../tests/host) compiles
the portable subsets against the `WOZ_PORT_HOST` backend.
