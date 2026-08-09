# Portable modules

`modules/` is the shared implementation of OpenAliro. The purity gate keeps the
portable source free of OS dependencies and ratchets a small explicit set of
legacy adapters. Backends for platform contracts live in `ports/`.

| Module | Responsibility |
|---|---|
| `woz_port` | OS, flash, logging, allocation, and byte-order contracts |
| `woz_aliro` | Aliro APDU, crypto, provisioning, reader, and approach logic |
| `woz_aliro_stack` | Adapter that links the portable Aliro stack into the Nordic application |
| `woz_aliro_ecp` | Enhanced Contactless Polling response for NFC presentation |
| `woz_uwb` | Aliro and FiRa UWB messages, sessions, ranging, and diagnostics |
| `woz_dw3000` | DW3000-family driver source sets and portable driver seams |
| `woz_nfc` | NFC transport abstraction and PN532 transport |
| `woz_matter` | Minimal Matter transport, session, and cluster implementation |
| `woz_anchor` | Anchor geometry, fusion, reporting, and SLAM logic |
| `woz_ml` | LOS/NLOS feature extraction and classifier |
| `woz_dfu` | Delta update receiver and applier |

## Public and private boundaries

- `include/` is the module's public API.
- `src/` is private implementation and may not be included by another module.
- `roles/*.list` is the authoritative source membership for selectable roles.
- `zephyr/` contains build-system discovery metadata, not an implementation
  backend.

`modules/woz_port/include/` is headers-only. Every implementation of those
contracts belongs in a port tree or the host test backend.
