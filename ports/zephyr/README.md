# Zephyr port

This directory is the Zephyr backend for OpenAliro. `zephyr/module.yml` exposes
the directory as one Zephyr module, with `CMakeLists.txt` and `Kconfig` selecting
the required backend sources.

Backend groups are organized by platform service:

- `osal/`, `log/`, and `store/` implement system contracts.
- `ble/`, `matter/`, and `nfc/` connect protocol transports.
- `dw3000/`, `uwb/`, and `drivers/` connect hardware.
- `dfu/` and `shell/` connect Zephyr subsystems to portable features.

Zephyr applications add this directory through `ZEPHYR_EXTRA_MODULES`. Reusable
protocol logic belongs in `modules/`, not in this tree.
