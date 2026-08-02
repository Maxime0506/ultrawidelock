# Configuring

Three layers: build options on the make command line, the Kconfig overlays
behind them, and runtime consoles on the running reader.

## Build options (nRF5340)

Set on the command line, e.g. `make build PRETTY=1 CHIP=dw3720`:

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

## Kconfig overlays

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

Every firmware has a serial console; no reflash needed.

**nRF5340** (`make nrf-term`): the `aliro` command group: `status`, `rx`,
`range`, `chip`, `selftest`, `log`, `frames`, `version`.

**ESP32 Matter lock** (`make monitor`): `status`, `lock`, `unlock`, `codes`,
`range`, `factoryreset`, `aliro <prov|trust|clear>`.

**ESP32 reader** (`make monitor`): `status`, `range`, `aliro-start` /
`aliro-stop` (demo responder, no phone needed), `aliro-prov`, `aliro-trust`.

`aliro trust` / `aliro-trust` persist the last-seen credential to NVS;
`factoryreset` and `flash-erase` drop it.

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
