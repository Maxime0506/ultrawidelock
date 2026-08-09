# Examples

`examples/` contains independently buildable firmware that demonstrates a
specific OpenAliro role or bench setup. Examples consume the same `modules/`
and `ports/` implementations as the lock products, but they are not complete
lock products themselves.

| Example | Purpose | Build command |
|---|---|---|
| [`zephyr/anchor/`](zephyr/anchor/) | Two-board DS-TWR anchor bench | `make anchor-build` |
| [`zephyr/nrf5340dk-initiator/`](zephyr/nrf5340dk-initiator/) | nRF5340 DK Aliro initiator | `make nrf-init-build` |
| [`esp32/reader/`](esp32/reader/) | Standalone ESP32 Aliro reader | `make esp-build APP=reader` |
| [`esp32/initiator/`](esp32/initiator/) | ESP32-S3 BLE initiator peer | `make esp-build APP=initiator TARGET=esp32s3` |

Framework names remain in this directory because each example is tied to one
build system. Complete products stay directly under `apps/` so users can find
them without first choosing a framework.
