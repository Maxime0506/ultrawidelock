# Ports

**The DWM3001CDK is not in this directory.** The primary target is
[`firmware/`](../firmware) at the top of the tree, built by the bare `make build`
and documented in [`firmware/README.md`](../firmware/README.md): the reader, a
Matter node and a Thread MTD in one nRF52833 image.

What lives here are the two **ports** of it, one directory per platform. Each
port reuses the platform-neutral engine in [`modules/`](../modules) and the
vendor driver in [`deps/dw3000`](../deps/dw3000), and keeps all target-specific
code inside its own directory.

The platform contract is [`modules/woz_port/include/woz_port.h`](../modules/woz_port/include/woz_port.h):
eight functions (heap, monotonic clock, two sleeps, cycle counter, a mutex), plus
`woz_log.h` for logging. A new RTOS is a new branch in those two headers; a new board
on an existing RTOS is a DW3000 SPI/GPIO backend. See [`docs/porting.md`](../docs/porting.md)
for the tiers and what each costs.

| Directory | Target | What it is | Status |
|---|---|---|---|
| [`../firmware/`](../firmware) | **DWM3001CDK** | *(not a port — the primary build)* Reader, Matter node and Thread MTD in one nRF52833 image. `make build` | **Hardware-validated.** Approach unlock plus a live Apple Home lock tile |
| [`nrf5340dk/`](nrf5340dk/) | **nRF5340 DK** | NFC tap + UWB approach unlock on top of the Nordic door-lock add-on, assembled by `make bootstrap` from the patches and overlays here. `make nrf-build` | Nordic-binary path hardware-validated end to end; source-stack default awaiting the full phone checklist |
| [`esp32/`](esp32/) | **ESP32-S3** | The complete ESP-IDF port: shared components plus three apps (a Matter door lock, a standalone bench reader, and a bench initiator) | **Hardware-validated.** Approach unlock driven end to end against a live iPhone, Wallet animation and all |
| [`esp32/`](esp32/) | **ESP32-C5** | The same apps with C5 pin, partition, and release-image support | **Build/release-supported.** No hardware validation is recorded |
| [`esp32/`](esp32/) | **ESP32-C6** | The same apps driving a BU04 over direct SPI, with `ST_NRST` held low | **Hardware-validated.** Approach unlock against a BU04 |

## The ESP32 port (`esp32/`)

One platform directory, three apps over one set of shared components:

```
esp32/
├── components/          # the stack, as ESP-IDF components:
│   ├── woz_uwb/         #   shared UWB engine + S3/C5/C6 DW3000 backend (SPI/GPIO/pins)
│   ├── aliro_crypto/    #   credential auth + ranging-key derivation (mbedTLS)
│   ├── aliro_reader/    #   reader, APDU codec, NVS-backed provisioning
│   ├── aliro_device/    #   the mirror of aliro_reader: the User-Device session layer
│   ├── aliro_ble/       #   NimBLE transport, peripheral side
│   ├── aliro_ble_central/ # NimBLE transport, the initiator's central side
│   └── piv_ccid/        #   optional native-USB PIV/CCID token (S3, bench only)
├── apps/
│   ├── matter-lock/     # the full lock: Matter commissioning + Wallet provisioning
│   ├── reader/          # standalone bench app: drives the stack without Matter
│   └── initiator/       # the phone's half of Aliro, to exercise a reader with no iPhone
└── test/                # host-runnable port tests (no ESP-IDF needed): test/run.sh
```

Start at [`esp32/apps/matter-lock/README.md`](esp32/apps/matter-lock/README.md) for the
whole lock, [`esp32/apps/reader/README.md`](esp32/apps/reader/README.md) for the
component stack and the bench app, or
[`esp32/apps/initiator/README.md`](esp32/apps/initiator/README.md) for the bench
initiator. All three apps consume `esp32/components/`; nothing is duplicated
between them.

What makes it more than a recompile: the reference design hands the credential
authentication and the ranging key derivation to a closed vendor library that only exists
as an ARM binary. It cannot be linked on either ESP target, so that layer had to be
reimplemented from scratch: the key schedule, the secure channels, the wire codec, and
the reader identity. Everything below the ranging key was already open in
`modules/woz_uwb` and compiles for all three ESP chips.

The S3 configuration has completed the live-iPhone hardware checklist, and C6 is
hardware-validated for approach unlock against a BU04. C5 is built and bundled by
the release workflow, including a browser-flash image, but bench validation
remains pending.

Two documents carry the detail:

- [`docs/esp32-gotchas.md`](../docs/esp32-gotchas.md) — every trap hit during bring-up,
  with symptom, cause, and fix. Read it before debugging anything on this target.
- [`docs/porting-esp32.md`](../docs/porting-esp32.md) — how the port was planned and
  how it actually went.

An early Zephyr-based ESP32-S3 spike (`ports/esp32s3/`, never run on silicon) was
removed; its pin map lives on in [`docs/esp32-bringup.md`](../docs/esp32-bringup.md).
For archaeology, the last commit carrying it is `b11549d`.

## The nRF5340 DK port

The only target with NFC. Its app is not in this repository: it is Nordic's
door-lock add-on, fetched pristine by `make bootstrap` and patched from
[`ports/nrf5340dk/patches/`](../ports/nrf5340dk/patches), configured by
[`ports/nrf5340dk/overlays/`](../ports/nrf5340dk/overlays), with the engine supplied from
`modules/` via `ZEPHYR_EXTRA_MODULES`. Its targets are `nrf-` prefixed
(`make nrf-build`, `make nrf-flash-erase`, `make nrf-term`). Details:
[`nrf5340dk/README.md`](nrf5340dk/README.md), and the top-level
[README](../README.md).
