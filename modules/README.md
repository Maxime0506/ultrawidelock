# Portable modules

`modules/` is the shared implementation of UltraWideLock. The purity gate keeps the
portable source free of OS dependencies and ratchets a small explicit set of
legacy adapters. Backends for platform contracts live in `ports/`. `ultrawidelock_` is
this project's namespace prefix: module directories, `ULTRAWIDELOCK_*` Kconfig symbols,
and Zephyr module names carry it so UltraWideLock code never collides with
Zephyr, NCS, or ESP-IDF names.

| Module | Responsibility |
|---|---|
| `ultrawidelock_port` | OS, flash, logging, allocation, and byte-order contracts |
| `ultrawidelock_cred` | Aliro APDU, crypto, provisioning, reader, and approach logic |
| `ultrawidelock_cred_stack` | Adapter that links the portable Aliro stack into the Nordic application |
| `ultrawidelock_uwb` | Aliro and FiRa UWB messages, sessions, ranging, and diagnostics |
| `ultrawidelock_dw3000` | DW3000-family driver source sets and portable driver seams |
| `ultrawidelock_nfc` | NFC transport abstraction, PN532 transport, and the RFAL-path ECP emitter |
| `ultrawidelock_matter` | Minimal Matter transport, session, and cluster implementation |
| `ultrawidelock_anchor` | Anchor geometry, fusion, reporting, and SLAM logic |
| `ultrawidelock_ml` | LOS/NLOS feature extraction and classifier |
| `ultrawidelock_dfu` | Delta update receiver and applier |

## Public and private boundaries

- `include/` is the module's public API.
- `src/` is private implementation and may not be included by another module.
- `roles/*.list` is the authoritative source membership for selectable roles.
- `zephyr/` contains build-system discovery metadata, not an implementation
  backend.

`modules/ultrawidelock_port/include/` is headers-only. Every implementation of those
contracts belongs in a port tree or the host test backend. Its
`include/ultrawidelock/ultrawidelock_hal.h` names the five chipset seams. Each module owns its
canonical SDK headers under its own `include/ultrawidelock/` directory.
