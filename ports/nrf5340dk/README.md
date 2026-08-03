# nRF5340 DK (the NFC target)

The second port, and the only board in this tree that unlocks on a tap: NFC tap + UWB
approach unlock. The primary board is now the DWM3001CDK
([`firmware/README.md`](../../firmware/README.md)), which does approach unlock with no
NFC front end at all. The hardware path here has been validated end to end with Nordic's
Aliro binary; the in-tree source stack is now the default and must pass
[`docs/hardware-validation.md`](../../docs/hardware-validation.md) before the next
release. Build it from the repository root, where every nRF5340 DK target is prefixed
`nrf-`:

```bash
make bootstrap     # fetch NCS + the Nordic add-on (~6.5 GB) into ./workspace
make nrf-build     # -> build/nrf5340dk/merged.hex
make nrf-flash
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
| `LTO=0` | opt out of link-time optimisation, **on by default** | the default is hardware-validated 2026-08-03: approach unlock, NFC tap, Apple Home commissioning and tile control |
| `DFU=0` | opt out of MCUboot plus Matter OTA, **on by default** | same run: MCUboot chains to a fully working lock. The OTA Requestor itself is still unexercised, and the bootloader signs with MCUboot's published demo key |

See [`docs/configuring.md`](../../docs/configuring.md) for the complete option
list and capture-safety warnings.

## Firmware size, and what the two size options buy

Measured on this port at NCS v3.3.0. The rows below isolate LTO on the
no-bootloader layout, so the app partition is the full 988 KB there; with the
default `DFU=1` it is 978,432 B and the numbers are in the next section. the net core (`ipc_radio`) is 256 KB and
neither size option changes it, because the net core is a separate sysbuild domain
and `EXTRA_CONF_FILE` does not cross into it. It DOES reach other images in the
application's own domain, which is a trap: see `DFU=1` below, where it silently
reached MCUboot.

| app core | FLASH | of 988 KB | RAM | of 448 KB |
|---|---|---|---|---|
| `DFU=0 LTO=0` (neither) | 872,284 B | 86.22% | 355,424 B | 77.48% |
| `DFU=0` (LTO only) | 794,832 B | 78.56% | 357,344 B | 77.89% |

LTO is worth 77,452 B of flash and costs 1,920 B of RAM.

**The gate that held it back has now passed.** Booting was never the bar:
whole-program codegen can move the UWB ranging path, whose arm deadline is
~1836 us, and only hardware can show that it did not. On 2026-08-03 a
`DFU=1 LTO=1` image was commissioned into Apple Home and then did approach
unlock, NFC tap, and lock/unlock from the Home tile. That covers the ranging
path and the tap path, which is the pair this board exists to prove.

It is now ON by default here, as it already was on the CDK. `LTO=0` opts out,
which is what you want when a stack trace has to name every frame. Re-run that
same walk-up after any change that could move the ranging path.

## Firmware update over the air (on by default)

MCUboot and the Matter OTA Requestor are built in unless you pass `DFU=0`. That
opt-out gives the older bench layout, where the app owns flash from `0x0` and
updates go over the probe with `make nrf-flash`: right for a board on a desk,
wrong for a lock on a door.

| image | FLASH | of | RAM | of |
|---|---|---|---|---|
| application | 905,096 B | 978,432 B (92.50%) | 358,560 B | 440 KB (79.58%) |
| application, `DFU=1 LTO=1` | 825,208 B | 978,432 B (84.34%) | 360,480 B | 440 KB (80.01%) |
| `ipc_radio` (net core) | 175,968 B | 222 KB (77.41%) | 57,468 B | 64 KB (87.69%) |
| `b0n` (net-core immutable bootloader) | 20,768 B | 34,176 B (60.77%) | 3,552 B | 64 KB (5.42%) |
| `mcuboot` | 24,724 B | 32 KB (75.45%) | 284,752 B | 440 KB (63.20%) |

**LTO more than pays for the bootloader.** MCUboot costs 33,280 B off the
partition and the OTA code adds 32,812 B to the image, 66,092 B of headroom in
total. LTO returns 79,888 B here. `DFU=1 LTO=1` therefore leaves 153,224 B free,
which is 13,796 B MORE free flash than the default bench build has today, while
also having a bootloader and an update path. That combination is the one to reach
for, which is why it is the default. Both halves are hardware-validated as a
working lock; what is still unproven is an actual OTA install.

**The second slot is free.** `mcuboot_secondary` lands on the DK's MX25R64 external
QSPI (`PM_MCUBOOT_SECONDARY_REGION=external_flash`), so dual-slot costs no internal
flash. What MCUboot does cost is 33,280 B off the front of the app core, which is
`mcuboot` (`0x8000`) plus `mcuboot_pad` (`0x200`); the app partition drops from
1,011,712 B to 978,432 B. The UWB engine still fits, with 73,336 B to spare.

**Both cores are in the update.** The build emits `dfu_multi_image.bin`,
`dfu_application.zip` and `matter.ota`. The manifest carries two images: the
application at `load_address` 33280, and `ipc_radio` as image index 1. The net core
is staged through `mcuboot_primary_1` in the `ram_flash` region and `pcd_sram`,
which is why the application's RAM region is 440 KB here rather than 448 KB, and
`b0n` is why the net core's flash region is 222 KB rather than 256 KB.

**To actually install an update** you need an OTA Provider on the fabric. `DFU=1`
turns on the Requestor half only (`CONFIG_CHIP_OTA_REQUESTOR=y`), which queries a
Provider and downloads from it; it does not serve anything. In practice that means
running `connectedhomeip`'s `ota-provider-app` with the generated `matter.ota`,
commissioning it onto the same fabric, and pointing the lock at it. Apple Home does
not serve arbitrary firmware, so on an Apple-Home-commissioned lock this needs a
second admin on the fabric. SMP/mcumgr over Bluetooth, which nRF Device Manager
speaks, is the other option and is deliberately NOT enabled here: it would need
`CONFIG_CHIP_DFU_OVER_BT_SMP=y`, and it brings an unpaired writable endpoint with
it.

**Switching a board between the two layouts costs its reader storage.**
`external_nvs` sits at `0x0` in the bench map and at `0x12f000` in the DFU map, so
the Aliro reader's external NVS is not where the other build expects it.

**The signing key is MCUboot's published one, and that is the thing to fix before
this goes on a door.** `DFU=1` leaves `CONFIG_BOOT_SIGNATURE_KEY_FILE` at the
upstream default, `bootloader/mcuboot/root-ec-p256.pem` in the fetched workspace,
which is a private key published in MCUboot's own repository. Anyone can sign an
image the resulting bootloader will accept. The DWM3001CDK does not have this
problem because `firmware/sysbuild.cmake` refuses to build against any of the
published demo keys and `make dfu-key` generates a per-checkout one
(`firmware/keys/README.md`); the equivalent has deliberately not been invented
here, because a second key mechanism is exactly the duplication this port avoids.

**Hardware-validated 2026-08-03**, `DFU=1 LTO=1` on an nRF5340 DK: MCUboot
verifies the image and chains to an application that commissions into Apple Home
and then does approach unlock, NFC tap, and lock/unlock from the Home tile. The
bootloader costs the lock nothing that anyone can see.

What that run does NOT cover is the OTA Requestor itself, because no update has
been installed. Booting with the Requestor compiled in is not the same as
downloading and applying an image, and the second half is the part that can
brick a board.

Getting there turned up two bugs worth knowing about, because both present as a
completely inert board.

**1. RAM power-down under the heap.** See `overlays/woz-aliro.conf`. This one broke
the default build too, not just `DFU=1`, and it is why MCUboot appeared to be at
fault: RAM block power survives a soft reset, so an application that powered
blocks down left MCUboot booting into the same holes, where it bus-faulted in the
heap its ECDSA verification uses. One fix cured both.

**2. `EXTRA_CONF_FILE` is not application-only.** Sysbuild forwards an
un-namespaced value to every image in the same domain, so `LTO=1` was also
building MCUboot with `CONFIG_ISR_TABLES_LOCAL_DECLARATION`. The port never
noticed because it had no second app-core image until `DFU=1` created one. The
build now forces both symbols off for `mcuboot` and fails if `CONFIG_LTO=y` shows
up in `mcuboot`, `b0n` or `ipc_radio`.

**MCUboot cannot tell you when it fails.** It is built with no console, and
`MCUBOOT_LOG=1` (which adds one) overflows the add-on's 32 KB `mcuboot` partition
by 4,084 B, because MCUboot already sits at 91.15% of that slot. Anything needing
the bootloader to talk has to start by giving it room, which means this port
taking over the flash map instead of borrowing the add-on's.

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
   `diag-latency.conf`) that `make nrf-build` options select.
4. `make nrf-build` (see [`scripts/build-nrf5340dk.sh`](../../scripts/build-nrf5340dk.sh)) drives `west build`
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
