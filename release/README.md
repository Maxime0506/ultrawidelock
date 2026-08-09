# Release bundle support

`release/` contains the documentation templates and flash helpers packaged with
published firmware. It is source material for `make release`, `make nrf-release`,
and `make esp-release`, not the main build-output directory.

Each board directory contains:

- `README.txt` for the shortest release summary.
- `FLASH.md` for board-specific wiring and flashing instructions.
- `flash.sh` for the corresponding release image.

Generated bundles are written below `build/release/` before publication.
