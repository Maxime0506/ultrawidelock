# nRF5340 DK (primary target)

The primary build: NFC tap + UWB approach unlock. The hardware path has been validated
end to end with Nordic's Aliro binary; the in-tree source stack is now the default and
must pass [`docs/hardware-validation.md`](../../docs/hardware-validation.md) before the
next release. Build it from the repository root:

```bash
make bootstrap     # fetch NCS + the Nordic add-on (~6.5 GB) into ./workspace
make build         # -> build/merged.hex
make flash
```

## Build variants

The default RFAL path with X-NUCLEO-NFC12A1/ST25R300 is hardware-validated.
The alternatives below have narrower evidence and must not be described as
hardware-validated without a new checklist run.

| Option | Selection | Evidence |
|---|---|---|
| `ALIRO_SOURCE=0` | legacy Nordic binary instead of the default source stack | diagnostic comparison and regression isolation |
| `NFC=pn532` | in-tree PN532 SPI transport | driver and APDU host tests only |
| `NFC=none` | no NFC reader; BLE/UWB remains enabled | source-build option only |
| `ALIRO_TRACE=1` | temporary session trace | currently unavailable because the required vendor trace patch is absent |
| `CIR=1` | CIA/CIR capture commands | diagnostic; costs walk-up latency while armed |

See [`docs/configuring.md`](../../docs/configuring.md) for the complete option
list and capture-safety warnings.

## Where the code is

There is intentionally no application source in this directory. The application is
Nordic's door-lock add-on, and this repo never edits fetched upstream trees. Instead
the target is assembled in layers, from pristine upstream up:

1. `make bootstrap` (see [`scripts/bootstrap.sh`](../../scripts/bootstrap.sh)) fetches
   the add-on at a pinned revision, plus NCS + Zephyr via its west manifest, into the
   git-ignored `workspace/`.
2. [`patches/`](patches/) are then applied to the fetched trees: the big one
   (`custom_impl-uwb.patch`) replaces the add-on's closed UWB backend with the open
   engine in [`modules/woz_uwb`](../../modules/woz_uwb); the rest are targeted fixes
   (time-sync persistence, DFU flash guards, the Approach Direction cluster, console
   curation, optional Home Assistant data-model support).
3. [`overlays/`](overlays/) configure the build without touching any fetched file:
   `dw3000-nfc.overlay` (devicetree: DW3110 on SPIM4, ST25R300 NFC on SPIM1, the
   pin map), `woz-aliro.conf` (Kconfig for the UWB responder + Aliro), `pm_static.yml`
   (flash layout), plus reader and diagnostic layers (`st25r.conf`,
   `pn532.overlay`, `woz-pretty.conf`, `woz-ha.conf`, `diag-cirdiag.conf`,
   `diag-latency.conf`) that `make build` options select.
4. `make build` (see [`scripts/build-nrf5340dk.sh`](../../scripts/build-nrf5340dk.sh)) drives `west build`
   with those overlays and injects the in-repo engine via `ZEPHYR_EXTRA_MODULES`
   (`modules/woz_uwb`, `modules/woz_aliro_stack`, `modules/woz_aliro_ecp`,
   `modules/woz_nfc`, `deps/dw3000`). The in-tree Aliro stack is the default and
   the build rejects a link map containing members from Nordic's
   `libaliro_ble.a`. `ALIRO_SOURCE=0` retains that binary only as a diagnostic
   fallback.

So the split is: shared engine in `modules/`, everything nRF5340-specific in this
directory, build orchestration in `scripts/`, and the fetched app in `workspace/`
(never committed, never edited in place, reproducible from `make bootstrap`).

CI verifies on every change that the patches still apply cleanly to the pinned
upstream revisions (`tests/tooling/patch_drift_check.sh`, no workspace needed).

## Hardware

| Part | Role |
|---|---|
| nRF5340 DK | Host SoC: BLE + Matter and the ranging engine |
| DWM3000EVB (DW3110) | UWB radio, on the Arduino header (SPIM4) |
| X-NUCLEO-NFC12A1 (ST25R300) | NFC reader front end for tap (SPIM1) |

Pin assignments live in [`overlays/dw3000-nfc.overlay`](overlays/dw3000-nfc.overlay).
Wiring and unlock troubleshooting: [`docs/troubleshooting.md`](../../docs/troubleshooting.md).
