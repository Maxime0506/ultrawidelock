# Configuring

Three layers: build options on the make command line, the Kconfig overlays
behind them, and runtime consoles on the running reader.

Bare make targets mean the DWM3001CDK, the primary board. The nRF5340 DK is
`nrf-` prefixed and the ESP32 is `esp-` prefixed. `make` with no target prints
the grouped list, and each target's own comment block in `mk/*.mk` is the
authority for its options.

## Build options (DWM3001CDK)

Set on the command line, e.g. `make build RELEASE=1 SMP=1`:

| Option | Effect |
|---|---|
| `PRISTINE=1` | from-scratch build. Needed whenever a `-D` flag below changes, because `-p auto` does not re-run CMake for those |
| `LTO=0` | opt out of link-time optimisation, which is on by default and worth 41,084 B. Use it when a stack trace has to name every frame |
| `RELEASE=1` | trade the 8 KB RTT ring for 7,168 B of RAM. A RAM lever only: codegen is identical either way |
| `SMP=1` | add mcumgr over Bluetooth, which is what nRF Device Manager speaks. Costs 3,712 B of RAM, so it wants `RELEASE=1` beside it |
| `DFU_LOG=1` | make the bootloader narrate what it does with a staged patch. Read it with MCUboot's own ELF, not the application's |
| `CDK_BUILD=<dir>` | which build directory `flash`, `flash-erase` and `monitor` mean. Default `build/cdk-matter` |
| `CDK_RTT_BUILD=<dir>` | point `monitor` at a different image without moving what the flash targets write |
| `CDK_KEY=<path>` | the image-signing key. Must be absolute. Default `firmware/keys/mcuboot_ec_p256.pem`, created by `make dfu-key` |
| `CDK_DEPLOYED=<hex>` | the record of what the board is running, which every delta is computed against |
| `OTA_NAME=<name>` | the advertised name `make dfu` and `make ota-smp` connect to |
| `FOTA_VERSION=<x.y.z>` | the version stamped into the file `make fota` leaves for a phone |

`LTO=0` no longer fits the flash map: the image measures 446,380 B without it
against a 433,664 B `app` partition, and the build fails rather than ships. See
[`../firmware/pm_static.yml`](../firmware/pm_static.yml), which carries the
derivation of every number in that map.

`make fota` and `make ota-smp` set `SMP=1 RELEASE=1` themselves and build in
their own directory. That is deliberate rather than a convenience: a board
without SMP does not speak mcumgr at all, so inheriting a bare `make`'s defaults
would build the wrong image and then diff the board against it.

## Kconfig overlays (DWM3001CDK)

They live beside the application in [`../firmware`](../firmware) and are
selected by the options above:

- `overlay-thread.conf`: always applied by `make build`. The Matter node,
  OpenThread MTD/SED and SRP. `make reader` omits it, which is the whole
  difference between the two images.
- `overlay-release.conf`, `overlay-smp.conf`, `overlay-lto.conf`: `RELEASE=1`,
  `SMP=1` and the default `LTO=1`. Ordered so that later files win.
- `overlays/uwb-selftest.conf`: the `make selftest` image, which reads the
  DW3110's `DEV_ID` at boot and stops.
- `sysbuild/mcuboot.conf`: the bootloader's own configuration, which is a
  separate image and does not inherit the application's.

## Build options (nRF5340 DK)

Set on the command line, e.g. `make nrf-build PRETTY=1 CHIP=dw3720`:

| Option | Effect |
|---|---|
| `CHIP=dw3720` | build for the DW3720 (default: DW3000) |
| `PRETTY=1` | curated, quiet serial console |
| `SELFTEST=1` | radio TX/RX self-test at boot, no iPhone needed |
| `STRICT=1` | drop suspect UWB range blocks |
| `HA=1` | Home Assistant variant; needs `make bootstrap HA=1` too |
| `ALIRO_SOURCE=0` | use the legacy Nordic Aliro binary instead of the default in-tree stack; diagnostic comparison only |
| `ALIRO_TRACE=1` | declared temporary BLE/session boundary trace; currently unavailable because the required vendor integration patch is absent; see [Capture safety](#capture-safety) |
| `NFC=st25r` | use the default X-NUCLEO-NFC12A1/ST25R300 RFAL path; hardware-validated |
| `NFC=pn532` | use the in-tree PN532 SPI transport; driver and APDU layers are host-tested, not hardware-validated |
| `NFC=none` | build without an NFC reader; BLE/UWB remains enabled |
| `CIR=1` | compile CIA/CIR diagnostics; arm at runtime with `aliro cir on`, `aliro cir dump on`, or `aliro cir probe` |
| `PRISTINE=1` | force a clean rebuild |

### Kconfig overlays (nRF5340 DK)

They live in [`../ports/nrf5340dk/overlays`](../ports/nrf5340dk/overlays)
and layer over the stock Nordic app; each file documents every setting it
touches.

- `woz-aliro.conf`: always applied. UWB heap and threads, BLE time-sync,
  the Apple ECP Express tap, log levels.
- `st25r.conf` or `pn532.overlay`: selected by `NFC=st25r|pn532`; `NFC=none`
  selects neither reader.
- `woz-pretty.conf`, `woz-ha.conf`: opt-in via `PRETTY=1` / `HA=1`.
- `diag-cirdiag.conf`: opt-in via `CIR=1`; reading a CIR window costs walk-up
  latency while armed, so use it only for a capture run.
- `diag-latency.conf`: diagnostic only (`LAT=1` to `scripts/build-nrf5340dk.sh`),
  Matter debug logs for timing notification delays.

`CONFIG_WOZ_ALIRO_SOURCE_STACK=y` is the nRF default. `scripts/build-nrf5340dk.sh` sets it
explicitly and verifies the final link map contains no member from
`libaliro_ble.a`. Keep `ALIRO_SOURCE=0` for comparison and regression isolation,
not as the normal build.

## ESP32-S3, ESP32-C5, and ESP32-C6

One `idf.py menuconfig` option, **Enable Aliro over BLE + UWB** (default
on): it advertises the Aliro features so Apple Home can put a key in
Wallet. Commissioning is standard Matter over Wi-Fi; `codes` reprints the
QR URL and pairing code.

ESP32-S3 is hardware-validated. ESP32-C5 has source and release-build support.
ESP32-C6 is hardware-validated for direct-SPI BU04 bring-up with `ST_NRST`
held low. No C5 hardware validation is recorded.

## Runtime consoles

Every firmware has a console, and none of them needs a reflash to use.

**DWM3001CDK** (`make monitor`): read-only, and it is RTT over `probe-rs`, not a
serial port. There is no UART console on this board, because on a single-core
part the DW3110's delayed-transmit reply window cannot afford a blocking console
write. `make nrf-term` does not reach it. The Matter image has no shell at all,
by configuration: `CONFIG_ALIRO_PROV_CONSOLE=n` and `CONFIG_SHELL=n`, so
`aliro export` and friends do not exist there. Back up `settings_storage` over
SWD instead of trying to export from it.

**DWM3001CDK, `make reader` only**: hold **SW2 and tap RESET** for provisioning
mode, which brings up a USB CDC-ACM console on the second USB port with the
radios down. Commands: `aliro prov`, `aliro import <hex>`, `aliro export yes`,
`aliro erase yes`. Full walkthrough in
[`../firmware/README.md`](../firmware/README.md).

**nRF5340 DK** (`make nrf-term`): the `aliro` command group: `status`, `rx`,
`range`, `chip`, `selftest`, `log`, `frames`, `version`.

**ESP32 Matter lock** (`make esp-monitor APP=matter-lock`): `status`, `lock`,
`unlock`, `codes`, `range`, `factoryreset`, `aliro <prov|trust|clear>`.

**ESP32 reader** (`make esp-monitor APP=reader`): `status`, `range`,
`aliro-start` / `aliro-stop` (demo responder, no phone needed), `aliro-prov`,
`aliro-trust`.

`aliro trust` / `aliro-trust` persist the last-seen credential to NVS;
`factoryreset` and `esp-flash-erase` drop it.

## Capture safety

When its missing integration patch is restored, `ALIRO_TRACE=1` logs protocol
states, message metadata, device or credential identifiers, and a truncated URSK
fingerprint. It does not log the raw URSK, but the trace is still a bring-up
artifact: do not ship it in production firmware or publish a capture without
review. In the current tree, selecting this option stops before the firmware
build because `integration/patches/aliro-ble-trace.patch` is absent.

Flight-recorder data is more sensitive. Raw serial logs containing `[FREC]`
records and binary `.frc` files include the full ephemeral URSK. Keep them
private and delete unneeded copies. The fuzz corpus exported by
`tools/flight_recorder.py` contains received frames only and excludes the URSK.
See [`SECURITY.md`](../SECURITY.md).

## Where the defaults are

Reader identity and the trust store are NVS-backed, created on first boot;
inspect or reset them from the consoles, nothing on disk to hand-edit.
