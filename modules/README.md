# Modules

The project-owned module layer. Portable modules compile unchanged on nRF5340/Zephyr,
ESP32-S3/ESP-IDF, and in the host test build; the nRF-specific Aliro replacement is kept
here because it implements a module API while hardware integration stays under
[`ports/`](../ports). The `woz_` prefix is this project's namespace, distinguishing it
from the fetched Nordic add-on and the vendored Qorvo driver; the `aliro_*` names are the
protocol-facing APIs those modules export.

Dependency direction, bottom-up (each layer depends only on the ones above it):

| Module | What it is | Depends on |
|---|---|---|
| [`woz_port/`](woz_port/) | The platform contract: `woz_port.h` (eight functions + a mutex) and `woz_log.h`. The first thing to read when porting. | nothing |
| [`woz_uwb/`](woz_uwb/) | The UWB engine: DW3000 driver glue, FiRa MAC, CCC key ladder + STS, DS-TWR responder, M1-M4 ranging-setup codec. Public API: `src/facade/woz_uwb_facade.h`. | `woz_port`, `deps/dw3000` |
| [`woz_aliro/`](woz_aliro/) | The Aliro credential-auth reader: key schedule, secure channels, BER-TLV/APDU wire codec, reader identity + trust store. Public API: `include/aliro_*.h`. BLE transport and storage backends are per-port. | `woz_port`, `woz_uwb` (ranging glue only) |
| [`woz_aliro_stack/`](woz_aliro_stack/) | The default nRF Aliro source stack, replacing Nordic's binary while retaining its public headers and app integration. | Zephyr/NCS, `woz_uwb` |
| [`woz_aliro_ecp/`](woz_aliro_ecp/) | NFC ECP emitter for the Express Mode tap (Nordic-licensed vendor code, nRF only). | Zephyr/NCS |

Build integration: on Zephyr the modules ride `ZEPHYR_EXTRA_MODULES`; the nRF build
enables `woz_aliro_stack` by default and the other modules through their `CONFIG_WOZ_*`
gates. On ESP-IDF the components under
[`ports/esp32/components/`](../ports/esp32/components) compile the portable sources; the
host suite in [`tests/host`](../tests/host) compiles them against the `WOZ_PORT_HOST`
backend.
