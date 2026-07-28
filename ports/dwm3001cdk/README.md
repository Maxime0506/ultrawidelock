# DWM3001CDK — standalone Aliro reader

One board. The nRF52833 runs the BLE peripheral and the Aliro reader engine;
the DW3110 in the same DWM3001C module does the UWB ranging. No host MCU board,
no seated DWM3000EVB, no ribbon wiring.

What that buys, versus the other two ports:

| | nRF5340 DK | ESP32-S3 | DWM3001CDK |
|---|---|---|---|
| Boards to wire | 2-3 | 2 | **1** |
| BLE + UWB unlock | yes | yes | yes |
| NFC / Express Mode | yes | yes | **no hardware** |
| Radio cores | 2 (BLE has its own) | 2 | **1, shared** |
| Debugger | external | external | **J-Link OB on board** |

The missing NFC is not a software gap: the CDK has no reader IC, and the
nRF52833's own NFC peripheral is tag-emulation only, so it can be read but
cannot read. BLE + UWB walk-up is the whole feature set here.

## Build

```sh
west build -p always -b decawave_dwm3001cdk -d ports/dwm3001cdk/app/build ports/dwm3001cdk/app
west flash -d ports/dwm3001cdk/app/build
```

Logging is RTT, not UART: on a single-core part the DW3110 delayed-TX reply
window cannot afford a blocking console write.

```sh
JLinkRTTLogger -Device NRF52833_XXAA -If SWD -Speed 4000 -RTTChannel 0 /dev/stdout
```

## Size, measured

Stage 0 built the whole thing to find out whether it fits. It does, with room
to spare:

| | Used | Available | |
|---|---|---|---|
| Flash | **236,492 B (231 KB)** | 504 KB | 45.8% |
| RAM | **70,964 B (69.3 KB)** | 128 KB | 54.1% |

Cross-checked with `arm-zephyr-eabi-size` (236,484 B / 70,946 B — the few bytes
of difference are alignment padding), and the engine is confirmed present in
`.text` rather than garbage-collected: `aliro_ranging_*`, `ccc_derive_*`,
`fira_session_*`, `aliro_uwb_msg_build_m1`, `dwt_initialise`.

The planning estimate was ~442 KB. It was roughly double the truth.

### Partitions are Partition Manager's, not devicetree's

The board DTS carries an MCUboot dual-slot map whose app slot is only 224 KB,
which would not have been enough. It does not apply: NCS builds this under
Partition Manager, which ignores DTS partitions and derives its own map. With
`CONFIG_BOOTLOADER_MCUBOOT=n`:

| Partition | Range | Size |
|---|---|---|
| app | 0x00000..0x7e000 | 504 KB |
| settings_storage | 0x7e000..0x80000 | 8 KB |

Verified by building with and without a DTS partition override: `partitions.yml`
and the memory report came out byte-identical both ways. To change the map, add
a `pm_static.yml`; a DTS override does nothing.

8 KB of settings storage is NVS's two-sector minimum. The provisioning blob is
476 B, so it fits one 4 KB sector with room for wear levelling.

No MCUboot means no DFU, which is fine on a board with a J-Link OB.

## Wiring (all internal to the module — nothing to solder)

Cross-checked between the upstream Zephyr board files and Qorvo's own
`uwb_stack_llhw.cmake` from DW3_QM33_SDK 1.1.1, which agree on every pin:

| Signal | Pin |
|---|---|
| SPI3 SCK | P0.03 |
| SPI3 MOSI | P0.08 |
| SPI3 MISO | P0.29 |
| SPI3 CS | P1.06 |
| IRQ | P1.02 |
| RESETn | P0.25 |
| WAKEUP | P1.19 |

## Status

Stage 0 of `internal/dwm3001cdk-reader-plan.md`: the image links and the size is
measured. Stages 1-6 (board bring-up, on-target EC self-test, DW3110 DEV_ID,
BLE transport against the ESP32-S3 initiator rig, iPhone walk-up, contention
tuning) follow from there.
