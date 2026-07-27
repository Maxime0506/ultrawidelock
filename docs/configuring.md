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
| `PRISTINE=1` | force a clean rebuild |
| `ALIRO_SOURCE=0` | use the legacy Nordic Aliro binary instead of the default in-tree stack |

## Kconfig overlays

They live in [`../ports/nrf5340dk/overlays`](../ports/nrf5340dk/overlays)
and layer over the stock Nordic app; each file documents every setting it
touches.

* `woz-aliro.conf`: always applied. UWB heap and threads, BLE time-sync,
  the Apple ECP Express tap, log levels.
* `woz-pretty.conf`, `woz-ha.conf`: opt-in via `PRETTY=1` / `HA=1`.
* `diag-latency.conf`: diagnostic only (`LAT=1` to `scripts/build.sh`),
  Matter debug logs for timing notification delays.

`CONFIG_WOZ_ALIRO_SOURCE_STACK=y` is the nRF default. `scripts/build.sh` sets it
explicitly and verifies the final link map contains no member from
`libaliro_ble.a`. Keep `ALIRO_SOURCE=0` for comparison and regression isolation,
not as the normal build.

## ESP32-S3

One `idf.py menuconfig` option, **Enable Aliro over BLE + UWB** (default
on): it advertises the Aliro features so Apple Home can put a key in
Wallet. Commissioning is standard Matter over Wi-Fi; `codes` reprints the
QR URL and pairing code.

## Runtime consoles

Every firmware has a serial console; no reflash needed.

**nRF5340** (`make term`): the `aliro` command group: `status`, `rx`,
`range`, `chip`, `selftest`, `log`, `frames`, `version`.

**ESP32 Matter lock** (`make monitor`): `status`, `lock`, `unlock`, `codes`,
`range`, `factoryreset`, `aliro <prov|trust|clear>`.

**ESP32 reader** (`make monitor`): `status`, `range`, `aliro-start` /
`aliro-stop` (demo responder, no phone needed), `aliro-prov`, `aliro-trust`.

`aliro trust` / `aliro-trust` persist the last-seen credential to NVS;
`factoryreset` and `flash-erase` drop it.

## Where the defaults are

Reader identity and the trust store are NVS-backed, created on first boot;
inspect or reset them from the consoles, nothing on disk to hand-edit.
