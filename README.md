<div align="center">

<img src="assets/hero.gif" width="760" alt="A Wallet home key unlocking the lock on approach, recorded on real hardware"/>

<h1>UltraWideLock</h1>

**Portable firmware for NFC and UWB smart locks.**

<sub>Real hardware. A real Wallet key. A real walk-up unlock.</sub>

</div>

UltraWideLock is portable firmware for NFC and UWB smart locks. It implements
the CSA's door-lock credential protocol — BLE plus NFC (ECP) plus UWB ranging —
built and tested against Nordic's ncs-door-lock-and-access-control add-on. The
protocol implementation lives in `modules/`; supported operating systems and
chipsets are connected through thin backends in `ports/`.

The repository includes three complete lock applications and focused examples
for reader, initiator, and anchor roles.

## How a door opens

```text
    iPhone / Apple Watch              UltraWideLock on one board
   ┌────────────────────┐             ┌──────────────────────┐
   │  Wallet home key   │             │  nRF52833 + DW3110   │
   └──────────┬─────────┘             └───────────┬──────────┘
              │                                   │
   1  BLE     │  credential service 0xFFF2        │
              │ ─────────────────────────────────▶│
   2  Auth    │  AUTH0 → AUTH1 → EXCHANGE         │
              │ ◀────────────────────────────────▶│
              │  both ends now hold the URSK      │
   3  UWB     │  key ladder → STS → DS-TWR        │
              │ ◀────────── ranging ─────────────▶│
   4  Gate    │  range consistency agrees         │
              │      ──  U N L O C K  ──          │
   5  Matter  │  lock state over Thread           └──▶ Apple Home
              ╵
```

Local only. No app, no account, no cloud round trip.

## Use as an SDK

Application code includes only its role. A reader uses:

```c
#include <ultrawidelock/reader.h>
#include <ultrawidelock/uwb.h>
```

Initiators use `<ultrawidelock/device.h>`, and codec-only consumers use
`<ultrawidelock/tlv.h>`. Installed plain-CMake consumers may use the all-in-one
`<ultrawidelock/ultrawidelock.h>` when they intentionally want every declaration.

Port implementations use `<ultrawidelock/ultrawidelock_hal.h>`, which names the five chipset
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
| [`apps/dwm3001cdk-lock/`](apps/dwm3001cdk-lock/) | DWM3001CDK | credential UWB, Matter over Thread |
| [`apps/nrf5340dk-lock/`](apps/nrf5340dk-lock/) | nRF5340 DK, DWM3000EVB, NFC12A1 | credential UWB and NFC, Matter over Thread |
| [`apps/esp32-matter-lock/`](apps/esp32-matter-lock/) | ESP32-S3, ESP32-C5, or ESP32-C6 with DWM3000EVB | credential UWB, Matter over Wi-Fi |

<div align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/grid-demo-dark.webp">
  <source media="(prefers-color-scheme: light)" srcset="assets/grid-demo-light.webp">
  <img src="assets/grid-demo.webp" width="880" alt="Home Key setup, Approach Direction, provisioning, NFC tap, and lock-state notifications on live hardware"/>
</picture>
<br/>
<sub>Home Key · Approach Direction · provisioning · NFC tap · live lock state</sub>
</div>

Run `make help` for the complete command list.

### What this machine needs

```sh
make tools
```

That names every host tool, the targets each one gates, and what is installed
here. The host suites need a C compiler and `python3`; `llvm-cov` and `cbmc`
gate `make coverage` and `make cbmc`. Zephyr builds also need
[nRF Util](https://www.nordicsemi.com/Products/Development-tools/nrf-util),
which is what installs the NCS toolchain. Nothing beyond a compiler and
`python3` is required to run:

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

`make bootstrap` is a large first run: roughly 2 GB for the NCS v3.3.0
toolchain, once per machine, then a multi-GB `west update` into `./workspace`.
Set `ULTRAWIDELOCK_WS=/other/disk/ws` to put the workspace elsewhere. It is safe to
re-run, and an interrupted fetch resumes rather than starting over.

ESP32 builds use an installed ESP-IDF environment; the Matter lock also needs
esp-matter. Neither is pinned by this repository, and both paths default under
`$HOME/esp` (override with `IDF_EXPORT` and `ESP_MATTER_PATH`). The bench builds
against ESP-IDF v5.5.4 and esp-matter `93b1680`.

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
DW3000 integration in `modules/ultrawidelock_dw3000` is ISC (Bruno Randolf); the vendored
Qorvo UWB driver it wraps is LicenseRef-QORVO-2, which permits use only with
Qorvo integrated circuits, so binaries built with UWB support inherit that
hardware restriction. The full file-to-license mapping is in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
