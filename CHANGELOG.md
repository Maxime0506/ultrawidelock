# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0.0
the API and behavior may change in minor releases.

## [Unreleased]

### Added

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

### Changed

- `make bootstrap` now checks or installs the host tools and pinned NCS
  toolchain before fetching the workspace.
- Per-worktree workspace seeding and a tested, installable local verification
  toolchain reduce stale builds and CI-only failures.

### Fixed

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

[Unreleased]: https://github.com/openaliro/openaliro/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/openaliro/openaliro/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/openaliro/openaliro/releases/tag/v0.1.0
