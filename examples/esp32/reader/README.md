# ESP32 reader example

This is a standalone credential reader for ESP32-S3, ESP32-C5, or ESP32-C6 with a
DWM3000EVB. It exercises the reader, provisioning, BLE, and UWB paths without
the esp-matter lock application.

Build from the repository root:

```sh
make esp-build APP=reader TARGET=esp32s3
```

The local Makefile forwards the same targets:

```sh
cd examples/esp32/reader
make build TARGET=esp32s3
```

Use `make esp-flash APP=reader TARGET=esp32s3` to flash and
`make esp-monitor APP=reader TARGET=esp32s3` for the console. The optional
`presence` variant adds its presence diagnostics to a separate build tree.
