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
make cdk           # -> build-cdk/app/zephyr/zephyr.hex
make cdk-flash     # over the on-board J-Link OB
```

The image carries no credential and is the same for every board. Flashing it is
half the job: a fresh board holds the DEV identity until you provision it, which
is a runtime step over USB (see "Provision this board" below).

Equivalent by hand, from the west workspace:

```sh
west build -p always -b decawave_dwm3001cdk -d ../build-cdk ../ports/dwm3001cdk/app
west flash -d ../build-cdk
```

Logging is RTT, not UART: on a single-core part the DW3110 delayed-TX reply
window cannot afford a blocking console write.

```sh
JLinkRTTLogger -Device NRF52833_XXAA -If SWD -Speed 4000 -RTTChannel 0 /dev/stdout
```

### probe-rs is optional, and only for scripted memory access

The SEGGER commands above are the supported path. `probe-rs` 0.32.0 does drive
this board's onboard J-Link OB (`probe-rs info` reports `nRF52833_xxAA` over SWD
with no flash write and no reset), and it is worth having for the one thing the
SEGGER tools make awkward: reading and writing target memory from a shell
one-liner instead of a JLinkExe command file.

```sh
probe-rs read b32 <addr> 2   # e.g. the RTT ring's WrOff/RdOff, to zero a stale ring
probe-rs trace <addr>        # poll one g_dbg_* counter without spending RTT bandwidth
```

It does not replace `west flash` plus `JLinkRTTLogger`, and its RTT viewing has a
silent trap. `--scan-region` defaults to empty, and with no region probe-rs **does
not scan and does not poll RTT at all**: you get a clean attach and zero output,
which reads exactly like a dead board. Always pass the ELF, or `--scan-region ram`:

```sh
probe-rs attach --chip nRF52833_xxAA --scan-region ram ../build-cdk/zephyr/zephyr.elf
```

One process at a time owns the probe, and draining RTT advances the ring's read
pointer, so a second attach splits the log rather than duplicating it. To watch
from several terminals, let one process own the probe and `tail -f` its output
file.

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

## Bring-up: the DW3110 answers

```sh
west build -p always -b decawave_dwm3001cdk -d build-selftest . \
    -- -DEXTRA_CONF_FILE=overlays/uwb-selftest.conf
west flash -d build-selftest
```

Then open SEGGER RTT Viewer (device `NRF52833_XXAA`, SWD, 4000 kHz, auto-detect
control block) and reset the board. First boot on real hardware, 2026-07-29:

```
I: RESET on gpio@50000000 pin 25
I: WAKEUP on gpio@50000300 pin 19
I: DW3000 SPI (max 8MHz)
I: DW3000 raw DEV_ID = 0xdeca0302 (expect 0xDECA03xx)
I: IRQ on gpio@50000300 pin 2
```

`0xdeca0302` is a valid Decawave ID, so the pin table above is confirmed on the
part rather than merely cross-referenced between two documents.

The overlay logs at INF on purpose. Global `CONFIG_LOG_DEFAULT_LEVEL=4` turns on
debug logging for every Zephyr module, including the arch MPU code that runs on
each context switch, which floods RTT and wraps the buffer before anything
useful is readable.

## BLE: an iPhone sees the reader

Default build, no overlay:

```
I: bt_enable = 0
I: Aliro reader up; advertising (SPSM 0x0080)
I: aliro_reader_start: transport up (SPSM 0x0080)
```

An iPhone running nRF Connect connects and enumerates 0xFFF2 with both
characteristics, the reader-SPSM (Read) and the device-version (Write), and
**never prompts to pair** — Aliro runs its own secure channel, so the walk-up
must not require bonding.

Reading the reader-SPSM characteristic returns `00 80 02 01 00 01 01`:

| Bytes | Meaning |
|---|---|
| `00 80` | L2CAP SPSM 0x0080 |
| `02` | protocol-versions length |
| `01 00` | version 0x0100 (v1.0) |
| `01` | features length |
| `01` | bit0 = timesync procedure 0 |

That is byte-identical to what the ESP32-S3 NimBLE backend publishes, so the
Zephyr transport matches the shipped one on the wire and not merely in intent.

Both radios run at once: BLE advertising while the DW3110 sits in permanent SP0
receive listening for Apple Pre-POLL. That is idle coexistence; whether they
survive concurrent load is a separate question, answered at stage 4.

### RTT buffer size is load-bearing

`CONFIG_SEGGER_RTT_BUFFER_SIZE_UP=4096`, not the 1 KB default. RTT is this
board's only console and the default policy is NO_BLOCK_SKIP: once the buffer
fills, writes are silently discarded and the log truncates mid-line. That is
indistinguishable from a firmware hang and will send you chasing a bug that
does not exist.

The DWM3001C **does** have a 32.768 kHz crystal, so no LFCLK override is needed;
the stock `CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL` works.

## Apple Wallet credentials: transplanted, not commissioned

This board cannot be commissioned into Apple Home, and that is a memory fact
rather than a missing feature. Apple mints an Aliro credential only through
Matter commissioning of a Door Lock, and Nordic's most stripped *supported*
single-core Matter lock (LTO, no shell, no console, no serial, no logging)
measures **614,008 B flash / 162,164 B RAM**, against Nordic's published
597 KB / 158 KB for the same config. This board has 504 KB of app flash and
128 KB of RAM in total, so that image is 95.6 KB over on flash and 30.4 KB
over on RAM before a single line of reader or UWB code joins it. There is no
external-memory escape either: the nRF52833 has no QSPI, so no XIP.

So the credential is copied in from a board that *was* commissioned. A reader
identity is pure key material (group identifier, signing key,
GroupResolvingKey, and the phones' endpoint public keys) and none of it binds
to a particular SoC. A second board carrying the same identity is the same
reader as far as Wallet is concerned.

### The source has to be the ESP32-S3

`ports/esp32/apps/matter-lock` is the only image in this tree that both holds
an Apple-issued credential and stores it portably: the delegate writes an
`aliro_prov` ("APRV") blob into NVS under `aliro_prov`/`blob` in the clear, and
`aliro export` prints it.

The nRF5340 DK cannot be the source, even though it runs a Matter Aliro lock.
Nordic's `subsys/aliro/reader_storage` keeps the identifier in PSA Protected
Storage and imports the signing key into a persistent PSA key slot with
`PSA_KEY_USAGE_SIGN_HASH` and **no export flag**, so firmware cannot read the
private scalar back at all; its `reader private_key list` command prints only
the derived public key. The APRV serialiser is not even linked into that image.
Getting a blob out of it would mean defeating a non-exportable key slot, not
converting a format.

### Export from the commissioned board

Read-only, and the safer of the two, because it does not disturb a lock that
currently works:

```sh
esptool.py -p <PORT> read_flash 0 0x400000 flash.bin
tools/aliro_blob.py flash.bin --import-cmd
```

The tool finds every APRV blob in the dump (NVS keeps superseded copies), parses
each, and says whether it will actually unlock. On a board built with
`CONFIG_WOZ_ALIRO_CLONE=y` you can instead type `aliro export` on its console
and hand the hex string to the same tool.

Three things make a blob useless, and the tool names all three rather than
letting you discover them on hardware: it is the built-in DEV identity (the
source was never provisioned, or was factory-reset since), the GroupResolvingKey
is all zero (`SetAliroReaderConfig` never landed), or there are no trust anchors
(no phone key enrolled).

### Provision this board

The identity is per-device data in the settings store. It is never in the image,
so one build is the same for everybody and carries no key.

1. **Hold SW2 and tap RESET.** The board comes up with its radios down and a USB
   CDC-ACM console on the second USB port, the one wired to the nRF52833.
2. **Open that port.** `screen /dev/tty.usbmodem*` on macOS, `/dev/ttyACM*` on
   Linux. RTT shows `provisioning mode: USB console up, radios down`.
3. **`aliro import <hex>`** with the blob from the export step above.
4. **Reboot without SW2.** The reader starts on the imported identity.

| Command | Does |
|---|---|
| `aliro prov` | print the stored identity and whether it can actually unlock |
| `aliro import <hex>` | adopt an exported blob, refusing the three dead cases |
| `aliro export yes` | print the stored blob, reader private key and all |
| `aliro erase yes` | back to the DEV identity with no trust anchors |

`import` runs the same three checks `tools/aliro_blob.py` runs on a flash dump,
and refuses **before** writing rather than leaving you to discover a dead
credential during a walk-up. A board that quietly adopted an unusable blob is the
exact failure this path exists to prevent.

Why a button rather than always-on USB: the device stack raises a start-of-frame
interrupt every millisecond, and this part runs BLE, the DW3110 and the app on
one M4, where commit 5b8d06b already had to fight for the delayed-TX reply
window. `usb_enable()` is therefore called only in provisioning mode, and an
operational boot never runs a USB interrupt next to a ranging round. The cost of
carrying the console in every image is 44,288 B flash and 19,648 B RAM, measured:
239,152 B / 74,164 B without it, 283,440 B / 93,812 B with it.

Two of those RAM bytes-per-thousand are worth naming, because both were found on
hardware and neither shows up in a build. The shell thread commits through a
software P-256 derive and peaks at 2,580 bytes, so the 2,048-byte default stack
overflowed; the MPU stack guard turned that into an aborted shell thread, which
presents as a live USB device with a dead console rather than as a crash. And the
serial backend's 64-byte RX ring buffer is one USB bulk packet, against an import
line of 577 characters, so a pasted blob lost characters in transit. Both are
sized explicitly in `prj.conf` with the measurement in the comment.

The provisioning-mode console is deliberately provisioning-only. `woz_uwb`'s
`aliro` command tree (status, chip, selftest) is switched off here with
`CONFIG_WOZ_UWB_SHELL=n`, because in this mode it would drive a radio that was
never started.

RTT capture on this board needs the control block address read out of the ELF
(`nm zephyr.elf | grep _SEGGER_RTT`) rather than J-Link's auto-search, which does
not find it here even with `-RTTSearchRanges` or `-RTTAddress`. The reliable route
is `savebin <file>, <up-buffer addr>, 0x1000` from J-Link Commander, where the
up-buffer address is the `pBuffer` word at control-block offset 0x1c.

### What had to be fixed for this to work at all

Two defects, both on the standalone path, either one enough to leave a perfectly
good credential inert:

- `aliro_reader_start` never applied the provisioned advertising parameters.
  Only `aliro_reader_start_attached` (the Matter path) did. A phone resolves
  "its" reader by re-deriving a dynamic tag from the GroupResolvingKey, so
  without them the board advertises the bare 0xFFF2 UUID and Wallet never
  approaches it. Nothing caught this because this is the first board to use the
  standalone entry point with a real identity.
- `aliro_reader_import_blob` did not refresh the advertisement after adopting an
  identity, though the Matter provisioning path does exactly that.

Covered now by `d.start_adv_params` and `d.import_refreshes_adv` in
`ports/esp32/test/test_aliro_reader.c`.

### Limits worth knowing before relying on it

- The trust store holds **4** phone keys (`ALIRO_TRUST_MAX`) while the Matter
  layer advertises 10. A fifth enrolled phone is accepted by Matter and silently
  dropped by the reader.
- Nothing revokes. `SetCredential` with `kAvailable` is deliberately not mirrored
  into the trust store, so a phone removed in Apple Home still opens this board
  until the store is cleared.
- The clone and the original are the same reader. Leaving the ESP32-S3
  commissioned is therefore a feature, not a leftover: it stays the Matter face
  of the lock, so Apple can still rotate key material into it and you re-export
  when it changes. A permanently offline clone cannot receive a rotation.

## Status

Against the stages in `internal/dwm3001cdk-reader-plan.md`:

| Stage | Check | |
|---|---|---|
| 0 | Fits: 236,768 B flash / 74,100 B RAM | done |
| 1 | BLE advert, iPhone enumerates 0xFFF2 | done |
| 2 | On-target EC self-test against Oberon | open |
| 3 | DW3110 DEV_ID `0xdeca0302`; TWR vs the DK rig | half done |
| 4 | ESP32-S3 initiator reaches ESTABLISHED | open |
| 5 | iPhone Wallet walk-up unlock | open |
| 6 | >= 95% ranging success over 100 walk-ups | open |

The open risk is unchanged and is not about memory: single-core radio
contention between the BLE controller and the DW3110's delayed-TX reply window.
Nothing so far exercises both under load.
