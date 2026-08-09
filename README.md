# OpenAliro

OpenAliro is portable firmware for Aliro NFC and UWB smart locks. The protocol
implementation lives in `modules/`; supported operating systems and chipsets
are connected through thin backends in `ports/`.

The repository includes three complete lock applications and focused examples
for reader, initiator, and anchor roles.

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

## Repository layout

| Directory | Purpose |
|---|---|
| [`apps/`](apps/) | Complete lock products |
| [`examples/`](examples/) | Independently buildable role and bench examples |
| [`modules/`](modules/) | Portable protocol and feature implementations |
| [`ports/`](ports/) | Zephyr and ESP32 backends for platform seams |
| [`integrations/`](integrations/) | Patches for external upstream applications |
| [`tests/`](tests/) | Host, shared, port, tooling, and on-target tests |
| [`cmake/`](cmake/) | Shared CMake helpers used by build definitions |
| [`mk/`](mk/) | Implementations behind the top-level Make targets |
| [`scripts/`](scripts/) | Setup, release, DFU, sizing, and device utilities |
| [`release/`](release/) | Templates and scripts included in release bundles |

Public module headers are under `modules/<name>/include/`. Private module
headers and implementation details remain under `modules/<name>/src/`.
