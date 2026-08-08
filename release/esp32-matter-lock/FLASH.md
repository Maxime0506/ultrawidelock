# openaliro on ESP32-S3/C5/C6: flash guide

The complete Aliro Matter lock on a single ESP32-S3, ESP32-C5 or ESP32-C6: it
commissions into Apple Home over Wi-Fi, your iPhone carries the key in Wallet,
and the lock opens as you walk up (BLE + UWB approach unlock; no NFC tap on
these targets). ESP32-S3 is the hardware-validated target. On C6 the UWB radio
is hardware-validated, but the full Matter walk-up on that chip is not; the C5
image is release-built only. See the Notes at the end for exactly what each
claim covers.

| File | What it is |
|---|---|
| `openaliro-matter-lock-esp32s3.bin` | merged S3 image (bootloader + partitions + app), flashed at 0x0 |
| `openaliro-matter-lock-esp32c5.bin` | merged C5 image, release-built but not hardware-validated |
| `openaliro-matter-lock-esp32c6.bin` | merged C6 image; UWB validated on this chip, full Matter walk-up not yet |
| `flash.sh` | flashes the image for whichever of the three chips you name |
| `manifest.json` | the chip list the browser flasher reads |
| `README.txt` | the short version of this guide, in plain text |
| `FLASH.md` | this guide |
| `VERSION.txt` | release tag, commit, and build date |
| `SHA256SUMS.txt` | checksums for every file above |

## 1. What you need

| Part | Role |
|---|---|
| ESP32-S3, ESP32-C5 or ESP32-C6 dev board | host: Wi-Fi, Matter, BLE, ranging engine. S3 validated on ESP32-S3-WROOM N16R8. C5 needs at least 8 MB flash and remains bench-pending. C6 targets the ESP32-C6-DevKitC-1 (8 MB) |
| Qorvo DWM3000EVB (DW3110) | UWB radio, hand-wired over SPI (11 connections) |
| 11 jumper wires | the SPI wiring |
| USB cable | power, flashing, serial console; must be a data cable |

Apple side: an iPhone with a UWB chip (iOS 26 is the validated floor), a Home hub
(HomePod or Apple TV), and a 2.4 GHz Wi-Fi network (the ESP32 has no 5 GHz).

## 2. Install the tools

1. Python 3 (`python3 --version` to check; on Windows from
   [python.org](https://www.python.org/downloads/) with "Add to PATH" ticked).
2. `pip install esptool` (`pip3` on some systems, `py -m pip` on Windows).
3. A serial terminal: `screen` (built into macOS and most Linux), `minicom`, or PuTTY.

Boards using native USB need no driver; boards with a CP210x or CH340 bridge
may need that vendor's driver on Windows and older macOS.

## 3. Wire it

> **Check the DWM3000EVB's power-select jumper first.** Wrong source and the radio
> fails silently (no device ID, a deaf responder), looking exactly like a software
> fault. It cost days on the bench once.

Power the EVB from **3V3**, not 5 V (the DW3000 is a 3.3 V part), share ground, and
wire per the build's source of truth `ports/esp32/components/woz_uwb/port/board_pins.h`:

| DWM3000EVB (Arduino pin) | Signal | ESP32-S3 | ESP32-C5 | ESP32-C6 |
|---|---|---|---|---|
| D13 | SCLK | GPIO12 | GPIO8 | GPIO6 |
| D11 | MOSI | GPIO11 | GPIO9 | GPIO7 |
| D12 | MISO | GPIO13 | GPIO23 | GPIO2 |
| D10 | CS | GPIO10 | GPIO10 | GPIO10 |
| D8 | IRQ | GPIO5 | GPIO5 | GPIO3 |
| D7 | RSTn | GPIO4 | GPIO4 | GPIO1 |
| D9 | WAKEUP | GPIO6 | GPIO6 | GPIO0 |
| 3V3 | power | 3V3 | 3V3 | 3V3 |
| GND | ground | GND | GND | GND |
| D1 | SPI-POL | GND (mode-0 strap) | same | same |
| D0 | SPI-PHA | GND (mode-0 strap) | same | same |

The two straps hold the DW3000 in SPI mode 0; skip them if your EVB revision already fixes the mode (check its manual).

The C6 image can also drive the DW radio inside a BU04 module over direct SPI,
which is how C6 was validated. That wiring differs from the table above; see the
[bring-up checklist](https://github.com/openaliro/openaliro/blob/main/docs/esp32-bringup.md)
for the BU04 pad map and the `ST_NRST` requirement.

## 4. Flash

Fastest for either chip: open the
[browser flasher](https://openaliro.github.io/openaliro/flash/) in Chrome or Edge,
select the board, and choose **Install**. The page and manifest are dry-checked,
but the repository does not yet record a successful real WebSerial flash.

With tools, the bundled helper handles all three chips:

```bash
bash flash.sh                    # asks which board you have
bash flash.sh --chip esp32c6     # or say so up front
```

esptool auto-detects the port; to name one, `bash flash.sh --chip esp32s3 /dev/ttyACM0`
(`/dev/cu.usbmodem…` on macOS, `COM…` on Windows). Chip not detected? Hold BOOT while
plugging in, retry, then replug.

Naming the chip matters: the images are not interchangeable, and writing the
wrong one gives a board that flashes cleanly and then never boots.

Before it writes anything, `flash.sh` checks this bundle against its own
`SHA256SUMS.txt` and refuses to continue if a file has changed. See "Check it
really came from us" below.

Manual equivalents:

```bash
esptool.py --chip esp32s3 --baud 460800 write_flash 0x0 openaliro-matter-lock-esp32s3.bin
esptool.py --chip esp32c5 --baud 460800 write_flash 0x0 openaliro-matter-lock-esp32c5.bin
esptool.py --chip esp32c6 --baud 460800 write_flash 0x0 openaliro-matter-lock-esp32c6.bin
```

Reflashing wipes any previous commissioning.

## 5. Watch it boot

Open the serial console at 115200 baud (after a BOOT-button flash, replug first):

- **macOS:** `screen /dev/cu.usbmodem<something> 115200`
- **Linux:** `screen /dev/ttyACM0 115200` (`/dev/ttyUSB0` on bridge boards)
- **Windows:** PuTTY, Serial, the board's COM port, 115200

The boot log prints the Matter onboarding QR code URL and manual pairing code, then a
`matter>` prompt; `codes` reprints both.

## 6. Add it to Apple Home

1. Open the QR code URL on another screen, or keep the 11-digit code handy.
2. Home app: add accessory, scan the QR code or type the code manually.
3. Keep the phone next to the board; commissioning starts over BLE, then joins your
   2.4 GHz Wi-Fi. Expect a minute or two.
4. Home warns about an uncertified accessory (test certificates); add anyway.
5. Home then provisions the key into Wallet on its own. Walk up: the Wallet animation
   plays, the lock opens, and it relocks as you leave.

The `matter>` console also has `status`, `range`, `lock`/`unlock`, and `factoryreset`.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| esptool "Failed to connect" or no port | Charge-only cable, wrong port, or hold BOOT while plugging in, then replug |
| No boot log | Wrong port, or still in download mode: replug |
| No valid UWB device ID | Power-select jumper (see Wire it), 5 V instead of 3V3, or a wrong SPI line |
| `range` shows nothing with the phone near | UWB wiring, or no key yet: finish commissioning, check Wallet |
| Home cannot find the accessory | No hub, Bluetooth off, or already commissioned (reflash or `factoryreset`) |
| Commissioning fails near the end | Wi-Fi join: 2.4 GHz required; remove the half-added accessory and retry |
| No key in Wallet | Wait a few minutes; needs iOS 26+ and a UWB iPhone |

More depth: the
[bring-up checklist](https://github.com/openaliro/openaliro/blob/main/docs/esp32-bringup.md)
and
[forty-odd known traps](https://github.com/openaliro/openaliro/blob/main/docs/esp32-gotchas.md).

## Check it really came from us

`SHA256SUMS.txt` proves your download arrived intact. It cannot prove who built
it: it travels in the same zip as the files it lists, so whoever could alter an
image could alter the checksums in the same motion.

Every file published with this release is separately signed at build time, by
the workflow that built it, using [Sigstore](https://www.sigstore.dev). That
signature is what proves origin, and anyone can check it without trusting the
release page or whoever handed them the zip:

```bash
gh attestation verify openaliro-matter-lock-esp32s3.bin --repo openaliro/openaliro
```

It prints the repository, the commit and the workflow run that produced the
file. It needs the [GitHub CLI](https://cli.github.com) and takes a few seconds.
`flash.sh` runs the same check for the image it is about to write, when the CLI
is installed, and says so either way.

You can check the zip itself the same way, before unzipping it:

```bash
gh attestation verify openaliro-esp32-matter-lock.zip --repo openaliro/openaliro
```

## Notes

- Evaluation firmware with Matter test certificates; do not put it in charge of a door you care about.
- Validation status differs per chip, and the wording is deliberate:
    - **ESP32-S3** has live-iPhone hardware validation of the whole path, including
      the Wallet walk-up unlock.
    - **ESP32-C6** is hardware-validated for UWB ranging, with a BU04 in direct-SPI
      mode and `ST_NRST` held low. The full Matter commissioning and Wallet walk-up
      on C6 has no recorded validation, so treat that part as untested.
    - **ESP32-C5** is build/release-supported with no recorded hardware validation
      at all, and must remain labelled bench-pending.
- Reflashing wipes commissioning; remove the stale accessory from Home before re-adding.
