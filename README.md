# UltraWideLock

UltraWideLock is portable firmware for Aliro NFC and UWB smart locks. The protocol
implementation lives in `modules/`; supported operating systems and chipsets
are connected through thin backends in `ports/`.

The repository includes three complete lock applications and focused examples
for reader, initiator, and anchor roles.

## Use as an SDK

Application code includes only its role. A reader uses:

```c
#include <ultrawidelock/reader.h>
#include <ultrawidelock/uwb.h>
```

Initiators use `<ultrawidelock/device.h>`, and codec-only consumers use
`<ultrawidelock/tlv.h>`. Installed plain-CMake consumers may use the all-in-one
`<ultrawidelock/ultrawidelock.h>` when they intentionally want every declaration.

Port implementations use `<ultrawidelock/woz_hal.h>`, which names the five chipset
seams for DW3000 GPIO/IRQ, DW3000 SPI, reader BLE, central BLE, and credential
storage. See [`PORTING.md`](PORTING.md) for the shortest board and chipset
workflow.

Full firmware is consumed through the Zephyr module or the ESP-IDF components.
Plain CMake consumers can install the public headers and portable TLV codec:

```sh
cmake -S . -B build/sdk -DCMAKE_INSTALL_PREFIX="$PWD/build/sdk-install"
cmake --build build/sdk
cmake --install build/sdk
```

The exported targets are `UltraWideLock::headers` and `UltraWideLock::tlv`. A complete
package-consumer example is under
[`examples/cmake/consumer/`](examples/cmake/consumer/).
The SDK version comes from the root `VERSION` file. Plain-CMake consumers may
request a compatible series or add this repository with `add_subdirectory`;
`make sdk-check` verifies both paths.

## Applications

| Application | Hardware | Connectivity |
|---|---|---|
| [`apps/dwm3001cdk-lock/`](apps/dwm3001cdk-lock/) | DWM3001CDK | Aliro UWB, Matter over Thread |
| [`apps/nrf5340dk-lock/`](apps/nrf5340dk-lock/) | nRF5340 DK, DWM3000EVB, NFC12A1 | Aliro UWB and NFC, Matter over Thread |
| [`apps/esp32-matter-lock/`](apps/esp32-matter-lock/) | ESP32-S3, ESP32-C5, or ESP32-C6 with DWM3000EVB | Aliro UWB, Matter over Wi-Fi |

Run `make help` for the complete command list. The shortest host-only check is:

```sh
make check
```

The Zephyr lock builds use the repository-managed NCS workspace and a local
signing key:

```sh
make bootstrap
make dfu-key
make build
```

ESP32 builds use an installed ESP-IDF environment. The Matter lock also needs
esp-matter:

```sh
make esp-build APP=matter-lock TARGET=esp32s3
```

With the reader and nRF5340 DK initiator attached, the unattended end-to-end
bench flow is:

```sh
make hitl
```

Pass script options through `HITL_ARGS`, for example
`make hitl HITL_ARGS=--skip-enrol`.

## Repository layout

| Directory | Purpose |
|---|---|
| [`apps/`](apps/) | Complete lock products |
| [`examples/`](examples/) | Independently buildable role and bench examples |
| [`modules/`](modules/) | Portable protocol and feature implementations |
| [`ports/`](ports/) | Zephyr and ESP32 backends for platform seams |
| [`integrations/`](integrations/) | Patches for external upstream applications |
| [`tests/`](tests/) | Host, shared, port, tooling, and on-target tests |
| [`include/`](include/) | SDK umbrella and public-API ownership guide |
| [`cmake/`](cmake/) | Shared CMake helpers used by build definitions |
| [`mk/`](mk/) | Implementations behind the top-level Make targets |
| [`scripts/`](scripts/) | Setup, release, DFU, sizing, and device utilities |
| [`release/`](release/) | Templates and scripts included in release bundles |

Canonical SDK headers are under each owner's
`modules/<name>/include/ultrawidelock/` directory. Other module contracts remain
under `modules/<name>/include/`. Private headers and implementation details
remain under `modules/<name>/src/`.

Coding agents should start with [`AGENTS.md`](AGENTS.md). It contains the
architecture invariants, task routing, and exact verification commands without
duplicating separate instructions for individual agent products.

## License

Project-original code is under the ISC license in [LICENSE](LICENSE). The
DW3000 integration in `modules/woz_dw3000` is ISC (Bruno Randolf); the vendored
Qorvo UWB driver it wraps is LicenseRef-QORVO-2, which permits use only with
Qorvo integrated circuits, so binaries built with UWB support inherit that
hardware restriction. The full file-to-license mapping is in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
