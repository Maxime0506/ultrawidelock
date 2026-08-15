# Security policy

UltraWideLock is lock firmware. A defect here can be the difference between a
door that opens for its owner and one that opens for anyone, so please report
suspected vulnerabilities privately rather than in a public issue.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting on this repository:
**Security → Report a vulnerability**. That opens a private advisory visible
only to you and the maintainers. No account beyond your GitHub login is needed.

Please do not open a public issue, pull request, or discussion for a suspected
vulnerability until an advisory for it has been published.

Useful things to include, in rough order of value:

- The commit or tag you tested, and the application and board.
- Which seam is involved: credential protocol, UWB ranging, BLE transport,
  Matter, DFU, or credential storage.
- A reproduction. A host-suite test case under `tests/host/` is the fastest
  possible report; a serial log or a described sequence is fine too.
- Whether the attack needs physical proximity, a paired credential, or neither.

## Supported versions

The SDK is pre-1.0. Only the latest release and `main` are supported; fixes are
not backported to earlier `0.x` series. See `VERSION` for the current version.

## Scope

In scope, as project-original code:

- The credential protocol implementation and its TLV codec in `modules/`.
- UWB ranging and the distance-bounding path, including replay and relay
  handling.
- Platform backends in `ports/`, and the five HAL seams named in `PORTING.md`.
- Signed update and rollback behavior, and anything that weakens the boot chain.
- Credential storage, key lifetime, and key material reaching a log or a
  console.

Out of scope here, because the code is not ours to fix. Please report these
upstream, though we are glad to know about them:

- The vendored Qorvo UWB driver under
  `modules/ultrawidelock_dw3000/dwt_uwb_driver/` — report to Qorvo.
- `modules/ultrawidelock_dfu/src/detools/` and its bundled heatshrink.
- nRF Connect SDK, Zephyr, ESP-IDF, esp-matter, OpenThread, Mbed TLS, and
  NimBLE — report to their respective projects.
- Findings that require a debugger on an unprotected part. Production images
  are expected to set access-port protection; `scripts/check-approtect.sh`
  exists for exactly that check.

## Hardening expectations

Anyone shipping this firmware is responsible for provisioning their own MCUboot
signing key, enabling access-port protection, and not shipping the demo
commissioning values. The repository's defaults are bench defaults.
