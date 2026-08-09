# Platform ports

`ports/` contains the thin platform backends used by the portable modules.
Each port may name exactly one operating system or framework.

| Port | Integration model |
|---|---|
| [`zephyr/`](zephyr/) | One Zephyr module providing Bluetooth, DFU, device, storage, shell, and OSAL backends |
| [`esp32/`](esp32/) | ESP-IDF components selected through component dependencies |

Product policy and portable protocol behavior do not belong here. A port should
translate a platform API into a contract declared by a portable module, then
return control to the shared implementation.

The host OSAL backend is under `tests/host/port/` because it also serves as the
test fake.

Use [`PORTING.md`](../PORTING.md) for the exact five-seam chipset contract and
the supported-framework integration flow.
