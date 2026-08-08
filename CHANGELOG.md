# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0.0
the API and behavior may change in minor releases.

## [Unreleased]

## [0.4.0] - 2026-08-08

### Added

- On-device UWB obstruction classifier: a LOS/NLOS decision tree trained on the
  public eWINE set restricted to the diagnostics a DW3000 can actually produce,
  shipped as `modules/woz_ml`. 390 B of flash, zero RAM, 28 B of stack, and the
  generated C is certified against its scikit-learn parent on every held-out
  sample rather than regression-pinned. The training pipeline and the bake-off
  that picked a tree over TFLM live in `ai/tinyml`.
- nRF5340 DK Aliro initiator, end to end: a Zephyr BLE central backend behind
  the same `aliro_ble_central.h` seam the ESP32 initiator uses, a DK app that
  reaches a shared URSK against an unmodified Apple-provisioned reader, and
  `scripts/aliro-enroll.py`, which enrols the bench initiator the way Apple
  Home enrols a phone: as a second admin over Matter. The CCC initiator ranging
  role itself is future work.
- Anchor-to-anchor DS-TWR: a top-level `anchor/` app that ranges two of this
  project's own boards against each other at the bare `WOZ_UWB` tier (no
  credential, no CCC engine in the image), plus `modules/woz_anchor`: door
  swing angle from ranged distance by hinge geometry and two-anchor
  side-of-door fusion, host-tested, default off, so every existing image stays
  byte-identical.
- An open commissioning window is now published as `_matterc._udp` over DNS-SD
  (both subtypes, fresh random instance name per window), withdrawn when the
  window closes. Until now the window was announced over BLE only, which Apple
  Home happens to use, so no other controller could ever add itself as a second
  admin.
- Status LEDs on the DWM3001CDK.
- `make flash` identifies the CDK among multiple attached J-Links by reading
  FICR INFO.PART through each candidate, caching the winner per bench, so two
  probes no longer require `CDK_PROBE=`.

### Changed

- The DWM3001CDK is now an rx-on-when-idle MED rather than a sleepy end device
  polling every 3,000 ms, and advertises SII=500 to match. Hub-to-node traffic
  (CASE sigmas, Home-tile invokes, subscription acks) no longer waits a mean
  1.5 s per frame, which was the largest share of tile-tap and commissioning
  latency. A battery build that wants SED back should take `POLL_PERIOD=500`
  with the advertised SII moved to match, never the old 3,000.
- `NumberOfAliroEndpointKeysSupported` now reports the trust store's real
  capacity (6) instead of a hard-coded 10 that let a controller install keys
  the store silently evicted.
- Trust-store blob v4 records the Matter credential type, credential index and
  user index each anchor was installed under, which is what makes revocation by
  name possible. v1-v3 blobs still parse; their anchors carry no binding, so
  re-install a key from the controller to make it revocable.

### Fixed

- Security: a home key the Matter admin removes now stops opening the door.
  The reader answers ClearCredential, ClearUser and a SetCredential that
  transitions to available by revoking the bound trust anchor, fail-closed:
  the key leaves the live store before the persist is attempted, and
  established Aliro sessions are swept off the ranging path. Previously Apple
  Home's key removal changed nothing the reader listened for, and the removed
  key kept opening the door until the whole trust store was cleared.
- Security: a revocation that reached RAM but not flash is no longer reported
  to the controller as success, on all four paths that did it, and the retry
  now rides the next repeated command rather than waiting for a disconnect
  that a Matter-only removal never gets.
- The DS-TWR time-of-flight estimator accumulated its numerator unsigned and
  underflowed near contact, returning kilometre-scale garbage inside roughly a
  metre, exactly where a lock spends its time. One shared signed estimator
  (`ds_twr_tof_signed`) now serves both the CCC responder and the anchor app;
  a negative near-contact result is a real measurement, and callers apply
  their own plausibility floor.
- Five places quoted CDK flash/RAM headroom the images have not had since the
  IP-layer drop; the budget prose now points at the shipping image's measured
  baseline.

### Removed

- The Discord bot moved to its own repository, along with its verify lane and
  docs-pipeline hooks. Nothing in the firmware tree referenced it, and its
  citation-drift gate was failing firmware pull requests over moved line
  numbers.

## [0.3.0] - 2026-08-05

### Added

- DWM3001CDK as the primary target: one nRF52833 image in `firmware/` carrying
  the Aliro reader, a Matter node and an OpenThread MTD, a reader-only build
  that needs no commissioner, and a CI job that compiles both.
- Apple Home commissioning on that board through a hand-written Matter node
  (`modules/woz_matter`) rather than CHIP, with a live lock tile.
- MCUboot on the DWM3001CDK, signed with a per-checkout key: `make dfu-key` is
  required once per clone before any CDK build will configure.
- Firmware update over Bluetooth as a signed delta, applied in place by the
  bootloader (`make dfu`), plus the same patch pushed from a phone over
  SMP/mcumgr (`make fota`, `make fota-done`).
- Independent `ALIRO_SOURCE=1` replacement for the Nordic Aliro binary, with a
  dedicated nRF CI build and host tests for its portable protocol layer.
- Selectable PN532 and no-reader NFC transports behind `modules/woz_nfc`; the
  PN532 driver and APDU adaptation are host-tested.
- Firmware-backed web twin: the shared UWB responder runs as WASM in the
  interactive walk-up page and is replayed in CI.
- Wireshark dissector for the clear-text Aliro BLE plane.
- Flight recorder for deterministic UWB replay and fuzz-corpus extraction, plus
  CIA/CIR channel diagnostics integrated with Aliro Lab.
- RSSI power gate and power-profile tooling for keeping UWB dark until a phone
  is close enough to approach.
- Predictive time-of-arrival approach logic, available when the RSSI gate is
  disabled, plus passive-carry research and the Aliro Gait offline analyzer.
- Expanded host, target-fake, backend, application-glue, and tooling suites;
  a 90 percent line-coverage floor; and a pre-push sweep with a serial tripwire
  followed by parallel lanes.
- `scripts/check-approtect.sh` gained a third layer, `--device <serial>`, which
  reads the readback-protection state out of the silicon rather than out of a file
  we wrote. The two config layers only ever answered "did our firmware ask for the
  lock"; a board can be locked with every config clean, and a locked nRF5340
  serves partial debug reads that look exactly like failing hardware. Run
  automatically after every nRF5340 DK flash.
- `make nrf-build LTO=1`: link-time optimisation for the nRF5340 DK application
  image, worth 77,452 B of app-core flash and costing 1,920 B of RAM. Off by
  default on that board and on by default on the DWM3001CDK, because only the
  CDK has a walk-up unlock behind it. The build reads both required Kconfig
  symbols back out of the linked image, so a silently dropped `CONFIG_LTO`
  fails the build instead of quietly measuring nothing.
- `make nrf-build DFU=1`: MCUboot and Matter OTA on the nRF5340 DK, restoring
  what the bench layout had switched off. The secondary slot lives on the DK's
  external QSPI, so dual-slot costs no internal flash, and the produced
  `dfu_multi_image.bin` / `matter.ota` carry both the application and the net
  core. The no-bootloader bench layout stays the default and stays selectable.
  `DFU=1 LTO=1` together leave 13,796 B more free flash than today's default
  build, so the bootloader more than pays for itself. That combination is
  hardware-validated (2026-08-03): commissioned into Apple Home, then approach
  unlock, NFC tap and Home-tile lock/unlock all working. Installing an OTA update
  is still unexercised.
- `scripts/check-signing-key.sh`: one refusal, shared by both Zephyr ports, for a
  bootloader that would trust a key it does not own. It holds MCUboot's list of
  seven published demo key files and four checks: nothing configured, a demo
  basename, a relative path (which resolves inside the MCUboot repository and
  becomes the demo key silently), and a path that is not there. `--self-test`
  proves each refusal still fires and that a valid key is still accepted.
  `firmware/sysbuild.cmake` runs it for the DWM3001CDK and
  `scripts/build-nrf5340dk.sh` for the nRF5340 DK, which cannot have a
  `sysbuild.cmake` of its own because its application is fetched upstream.

### Changed

- Breaking for existing nRF5340 DK boards: `make nrf-build` now defaults to
  link-time optimisation and to MCUboot plus Matter OTA. `LTO=0` and `DFU=0` opt
  out individually. The defaults are the configuration validated on hardware
  2026-08-03, and the pair leaves more free app flash than the old no-bootloader
  default did, because LTO's 77,452 B more than covers the bootloader's 33,280 B.
  Two consequences before you reflash a provisioned board: the flash map moves
  `external_nvs` from `0x0` to `0x12f000`, which costs that board its Aliro
  reader storage, and `make dfu-key` is now a prerequisite of `make nrf-build`
  the way it already was of `make build`. Use `DFU=0` to keep the old layout,
  which has no bootloader and needs no key.
- `make dfu-key` moved from the DWM3001CDK group to Setup, and the key it makes
  is now the checkout's rather than that board's: `SIGN_KEY` in the top-level
  `Makefile`, shared by both Zephyr ports. Same target, same path, same refusal
  to overwrite. `CDK_KEY` survives as the per-build override `make release` uses.
- Breaking: the bare make targets now mean the DWM3001CDK (`make build`,
  `flash`, `flash-erase`, `monitor`). The nRF5340 DK moved to `nrf-` prefixed
  targets (`make nrf-build`, `nrf-flash-erase`, `nrf-term`) beside the `esp-`
  prefixed ESP32 ones; `make term` still runs and prints where it went.
- `make bootstrap` now checks or installs the host tools and pinned NCS
  toolchain before fetching the workspace.
- Per-worktree workspace seeding and a tested, installable local verification
  toolchain reduce stale builds and CI-only failures.

### Fixed

- nRF5340 DK: the image did not boot. The add-on enables
  `CONFIG_RAM_POWER_DOWN_LIBRARY` (its `prj.conf`) and calls
  `power_down_unused_ram()` during Matter init, which switches off every RAM block
  above the image's data. That is where picolibc puts its malloc arena, since
  `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE` defaults to -1, so the heap bus-faulted
  writing into powered-down RAM. Disabled in `ports/nrf5340dk/overlays/woz-aliro.conf`.
  RAM block power survives a soft reset, so this also took MCUboot down on the
  following boot, inside the heap its signature check uses: one fix, both symptoms.
- `LTO=1` on the nRF5340 DK also built MCUboot with
  `CONFIG_ISR_TABLES_LOCAL_DECLARATION`, because sysbuild forwards an
  un-namespaced `EXTRA_CONF_FILE` to every image in the same domain. Only visible
  once `DFU=1` gave the port a second app-core image. The build now confines both
  symbols to the application and fails if `CONFIG_LTO=y` reaches `mcuboot`, `b0n`
  or `ipc_radio`.
- Reader relock/status delivery across disconnects, coalesced Aliro envelopes,
  URSK teardown, CIR capture timing, and diagnostic phase attribution.

## [0.2.0] - 2026-07-22

### Added

- ESP32-C5 as a second Matter-lock and bench-reader build target, with
  per-chip pins and partition layouts. Release-build support was proven;
  hardware validation remained pending.
- Dual-chip browser flasher with board auto-detection or explicit S3/C5
  selection, plus per-chip release image names.
- Aliro Lab structured traces and scored walk-up reports.
- Access Document step-up support and live-clock dynamic advertisement tags.
- Session PHY pre-warm and connection-interval work for sub-second approach.

## [0.1.0] - 2026-07-22

### Added

- First tagged nRF5340 DK and ESP32-S3 Matter Aliro lock firmware bundles.
- NFC tap, BLE/UWB approach unlock, walk-away relock, Matter commissioning,
  and Wallet provisioning on the hardware-validated configurations.
- Shared UWB engine and port contract, ESP32 reader stack, host KATs,
  sanitizers, fuzzing, bounded parser proofs, and firmware build gates.
- Zero-toolchain WebSerial flasher for the ESP32-S3 release image.

[Unreleased]: https://github.com/openaliro/openaliro/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/openaliro/openaliro/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/openaliro/openaliro/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/openaliro/openaliro/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/openaliro/openaliro/releases/tag/v0.1.0
