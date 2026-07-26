# nRF5340 bring-up

Parts on the bench to a healthy first boot; toolchain install is in
[set-up.md](set-up.md).

## Parts

| Part | Role |
|---|---|
| nRF5340 DK | Host SoC: BLE + Matter and the ranging engine |
| DWM3000EVB (DW3110) | UWB radio, on the Arduino header (SPIM4) |
| X-NUCLEO-NFC12A1 (ST25R300) | NFC reader front end for tap (SPIM1) |

Pin assignments live in
[`../ports/nrf5340dk/overlays/dw3000-nfc.overlay`](../ports/nrf5340dk/overlays/dw3000-nfc.overlay);
that file is the source of truth.

## Before powering

* 3.3 V only, never 5 V; share a common ground with the DK.
* Check the DWM3000EVB **power-select jumper** first: wrong position means
  SPI fails silently, no device ID, a responder that never listens.

## Build, flash, console

```bash
make build
make flash-erase
make term
```

The image lands in `./build/merged.hex`; the console is on VCOM1 (VCOM0 is
silent).

The first flash and any net-core config change need the erase; plain
`make flash` otherwise.

## First-boot checks

* `make selftest` exercises the radio with no phone present: it separates a
  wiring problem from a protocol one.
* On the shell: `aliro status`, `aliro chip` (DW3110 device ID over SPI),
  `aliro range`.
* A healthy release boot is clean on the console and starts BLE advertising.

## Prove the unlock

Commission into Apple Home, then run the pass criteria in
[hardware-validation.md](hardware-validation.md): tap, pocketed approach
unlock, walk-away relock.

## If something fails

SPI and ranging failure modes: [troubleshooting.md](troubleshooting.md).
