# Applications

`apps/` contains the complete UltraWideLock products. Each directory owns
the product-specific configuration, wiring, entry point, and build overlays.
Portable protocol code remains in `modules/`, while OS and chipset glue remains
in `ports/`.

| Application | Primary build command | Output root |
|---|---|---|
| [`dwm3001cdk-lock/`](dwm3001cdk-lock/) | `make build` | `build/cdk-matter/` |
| [`nrf5340dk-lock/`](nrf5340dk-lock/) | `make nrf-build` | `build/nrf5340dk/` |
| [`esp32-matter-lock/`](esp32-matter-lock/) | `make esp-build APP=matter-lock TARGET=esp32s3` | `build/esp32-matter-lock-esp32s3/` |

These directories are product front ends, not alternate copies of the shared
stack. New reusable behavior belongs in `modules/`; new platform integration
belongs in `ports/`.
