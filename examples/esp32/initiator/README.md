# ESP32-S3 initiator example

This is the ESP32-S3 bench peer for an credential reader. It implements the BLE
central and user-device transport path. The current example is BLE-only and
does not enable a UWB initiator.

Build it from the repository root with an installed ESP-IDF environment:

```sh
make esp-build APP=initiator TARGET=esp32s3
```

Use `make esp-flash APP=initiator TARGET=esp32s3` to flash and
`make esp-monitor APP=initiator TARGET=esp32s3` for the console. This example
does not require esp-matter.
