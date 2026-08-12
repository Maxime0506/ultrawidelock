# ESP32 Matter lock

This application combines the UltraWideLock reader with an esp-matter door lock.
It supports ESP32-S3, ESP32-C5, and ESP32-C6 boards connected to a DWM3000EVB.

## Build

Install ESP-IDF and esp-matter, then build from the repository root. The bench
builds against ESP-IDF v5.5.4 and esp-matter `93b1680`:

```sh
make esp-build APP=matter-lock TARGET=esp32s3
```

The default paths can be overridden when the SDKs are installed elsewhere:

```sh
make esp-build APP=matter-lock TARGET=esp32s3 \
  IDF_EXPORT=/path/to/esp-idf/export.sh \
  ESP_MATTER_PATH=/path/to/esp-matter
```

The local Makefile forwards the same targets, so this is equivalent:

```sh
cd apps/esp32-matter-lock
make build TARGET=esp32s3
```

Use `make esp-flash APP=matter-lock TARGET=esp32s3` to flash and
`make esp-monitor APP=matter-lock TARGET=esp32s3` for the console. Supported
variants include `presence`, `hamqtt`, and `piv`; run `make help` for their
dedicated targets.

## Contents

- `main/` contains the Matter application, lock policy, shell, and LED adapter.
- `sdkconfig.defaults*` contains shared and target-specific ESP-IDF settings.
- `partitions*.csv` contains flash layouts.
- Shared ESP32 components are under [`ports/esp32/`](../../ports/esp32/).
