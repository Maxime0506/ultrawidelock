<h1 align="center">openaliro</h1>

<p align="center"><strong>Build an Aliro lock for iPhone and Apple Watch.</strong><br/>Hands-free over BLE + UWB · One board · No app</p>

<p align="center"><a href="#the-board">The board</a> · <a href="#build-it">Build it</a> · <a href="#update-it-over-the-air">Update it</a> · <a href="#the-other-two-targets">Other targets</a> · <a href="https://openaliro.github.io/openaliro/">Documentation ↗</a></p>

<p align="center"><a href="https://github.com/openaliro/openaliro/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/openaliro/openaliro/ci.yml?branch=main&style=flat-square&label=ci" alt="ci"/></a> <a href="https://github.com/openaliro/openaliro/releases"><img src="https://img.shields.io/github/v/release/openaliro/openaliro?style=flat-square" alt="latest release"/></a> <img src="https://img.shields.io/badge/license-source--available-lightgrey?style=flat-square" alt="source-available license"/></p>

<p align="center"><img src="assets/hero.gif" width="720" alt="A real iPhone unlocking openaliro on approach"/><br/><sub>Real hardware · Real Wallet key · Real approach unlock</sub></p>

<p align="center"><a href="firmware/README.md"><kbd>The board →</kbd></a>&nbsp;<a href="web-twin/index.html"><kbd>Try web twin →</kbd></a>&nbsp;<a href="https://openaliro.github.io/openaliro/flash/"><kbd>Flash ESP32 →</kbd></a>&nbsp;<a href="https://openaliro.github.io/openaliro/"><kbd>Documentation →</kbd></a></p>

<p align="center">Lock-side <a href="https://csa-iot.org/all-solutions/aliro/">Aliro</a> firmware · BLE auth · UWB ranging · proximity unlock</p>

## The board

The headline target is a **Qorvo DWM3001CDK**. One nRF52833 (512 KB of flash,
128 KB of RAM), a DW3110 radio beside it inside the same DWM3001C module, and a
J-Link already on the board. Nothing to wire, nothing to solder, no second
development kit, no ribbon cable.

That single part carries all of this at once:

- the **BLE peripheral** an iPhone approaches and talks Aliro to,
- the **Aliro reader engine**: AUTH0 / AUTH1 / EXCHANGE, the CCC key ladder, STS, DS-TWR,
- a **hand-written Matter node** ([`modules/woz_matter`](modules/woz_matter/)), not CHIP,
- an **OpenThread MTD**, so the lock joins a real Thread network,
- the **DW3110's UWB ranging**, over the module's internal SPI.

Putting Matter on this part is the piece that was supposed to be impossible.
Nordic's most stripped *supported* single-core CHIP Matter lock (LTO, no shell,
no console, no logging) measures **614,008 B of flash and 162,164 B of RAM**.
This board has 504 KB of application flash and 128 KB of RAM in total, so that
image is 95.6 KB over on flash and 30.4 KB over on RAM before a single line of
reader or UWB code joins it, and the nRF52833 has no QSPI, so there is no
external memory to escape into. Writing the Matter node by hand is what closed
that gap. Apple Home commissions this board over BLE and then shows a live lock
tile, on the part that could not hold the stack it was meant to need.

### What has actually run on hardware

| | State |
|---|---|
| Reader, Matter node and Thread MTD in one image | fits: 409,988 B flash, 125,012 B RAM, LTO on by default |
| iPhone enumerates the Aliro `0xFFF2` service over BLE | done |
| DW3110 answers, live ranging against an iPhone | done, 565 cm down to 0 cm |
| An initiator reaches `ESTABLISHED` | done, the iPhone itself |
| **iPhone Wallet walk-up unlock** | **done**, four in one session, 2026-08-02 |
| Firmware update over Bluetooth | done, byte-identical result, 2026-08-03 |
| **≥ 95% ranging success over 100 walk-ups** | **open, never run** |

Everything above that last row has been demonstrated on real hardware.
Nothing has yet been demonstrated *at a rate*: the walk-up sample so far is
single digits, and closing it needs someone to walk at a door a hundred times.
Per-stage evidence, with the log lines, is in
[`firmware/README.md`](firmware/README.md).

### There is no NFC tap on this board

That is a hardware fact rather than a missing feature. The DWM3001CDK carries no
NFC reader IC, and the nRF52833's own NFC peripheral is tag emulation only, so
the part can be read but cannot read. BLE plus UWB walk-up is the whole feature
set here. If you want Express Mode over NFC,
[the nRF5340 DK](#the-other-two-targets) is the target that has it.

## Build it

```bash
git clone https://github.com/openaliro/openaliro.git
cd openaliro

make dfu-key      # once per clone   ·  this checkout's image-signing key
make bootstrap    # once per machine ·  host tools + pinned NCS + workspace (~8.5 GB)
make build        # -> build/cdk-matter
make flash        # over the on-board J-Link
make monitor      # the console, over RTT
```

`make dfu-key` comes first for a reason. Every image on this board is signed,
and with no key the build **fails at configure** rather than quietly falling
back to the demo key published in MCUboot's own repository. The key is
gitignored, so a fresh clone or a new worktree needs its own.

Bare `make build`, `make flash`, `make flash-erase` and `make monitor` all mean
this board and its Matter image. `make rebuild` forces a pristine build; there
are options for `PRISTINE=1`, `RELEASE=1` and `LTO=0` in `make` with no target.

**The setup code comes from the build, not from the board.** `make build` and
`make monitor` each end by printing the line Apple Home is about to ask you for,
because a Matter device stores the SPAKE2+ *verifier* and never the passcode, so
nothing on the device can print it. The code is re-derived on the host from the
configuration the build just produced, and checked against the verifier that was
compiled in. Add the accessory with "More options…" then "Enter Code": there is
no QR label on this board.

**`make reader` is the same source without Matter or Thread.** Aliro and UWB
only, no commissioner and no Thread network required, with the reader identity
typed in over USB. It is the fastest route to a working board and the right one
for bench work on the radio. It builds elsewhere on purpose, so the flash and
console targets keep meaning the Matter image:

```bash
make reader
make flash   CDK_BUILD=build/cdk-reader
make monitor CDK_RTT_BUILD=build/cdk-reader
```

`make selftest` is the bring-up image: it reads the DW3110's `DEV_ID` over SPI
at boot, which is how a wrong pin, a wrong SPI mode or an unpowered radio tells
you so in one line.

## Update it over the air

The board updates itself over Bluetooth, with no cable and no probe. Proven on
hardware on 2026-08-03: a patch went over the air and the board's flash came out
byte for byte identical to the target image (matching CRC on both sides).

Two full MCUboot slots want about 844 KB on a 512 KB part, and there is no
external flash to stage into, so what travels is a signed **delta**.
[`scripts/woz_patch.py`](scripts/woz_patch.py) builds and signs it,
[`scripts/woz_push.py`](scripts/woz_push.py) carries it over GATT, the
application stages it in a dedicated `patch_staging` partition and reboots, and
[`modules/woz_dfu`](modules/woz_dfu/) applies it from a `SYS_INIT` inside
MCUboot, because an application cannot rewrite the flash it is executing from.
The apply takes roughly 17 to 31 seconds.

```bash
make dfu     # build, diff against what the board runs, sign, push. One command.
make fota    # instead: the single file a phone can install, plus the steps
make fota-done   # after a phone push, confirm what the board is now running
```

An update needs an open window, and the window is the whole authorization model:
the patch is signed and MCUboot re-verifies the result before booting it, so no
peer can install code either way. What a closed window prevents is a stranger in
radio range spending your flash's erase cycles. Two ways to open it, both
confirmed on a live commissioned lock: **press SW2**, or use Apple Home's own
**"Turn On Pairing Mode"** (the node serves the AdministratorCommissioning
cluster). The board says so itself while the window is open, on its blue LED, so
a press that did not register is visible without a debugger attached.

`make fota-done` is not optional after a push from a phone. A delta is computed
against the exact bytes on the board, only the build host keeps that record, and
a push from the phone is invisible to it. Skip it and the *next* update is built
from the wrong base and refused.

## Things worth knowing before you rely on it

- **The console is RTT, over `probe-rs`, not UART.** There is no UART console on
  this board: on a single-core part the DW3110's delayed-transmit reply window
  cannot afford a blocking console write. `make monitor` attaches with the ELF,
  which must be the one you *flashed*. The RTT ring survives a reset on purpose,
  so the first block you see is the previous run: anchor on the
  `*** Booting nRF Connect SDK ***` line.
- **`make flash-erase` costs the commissioning.** It takes the Matter fabrics,
  the reader identity and its trust anchors, so Apple Home has to add the lock
  again. To clear only what a controller can see, hold **SW2 through reset**
  instead: same effect on the fabrics and the anchors, and it leaves the Thread
  settings alone.
- **APPROTECT must never be locked on this board.** Two independent guards fail
  the build if it is. Recovering debug access costs a mass erase of flash *and*
  UICR, which takes the reader's private key and every iPhone key ever
  provisioned against it.
- **Nothing revokes yet, and the trust store holds four phone keys.** A phone
  removed in Apple Home still opens the board until the store is cleared. Read
  [`firmware/README.md`](firmware/README.md) before trusting it with anything.

## The other two targets

Both are still here, both still build, and both share the same engine in
[`modules/`](modules/README.md). Neither is the headline any more.

### nRF5340 DK: the one with NFC

The only target with an Express Mode tap. It wants an nRF5340 DK plus a
DWM3000EVB or DW3110 plus an X-NUCLEO-NFC12A1 or ST25R300, and the wiring in
[`docs/nrf5340-wiring.md`](docs/nrf5340-wiring.md).

```bash
make bootstrap        # the same one-time setup
make nrf-build        # -> build/nrf5340dk/merged.hex
make nrf-flash-erase  # the first flash
make nrf-term         # serial console
```

The in-tree Aliro stack is the default; `ALIRO_SOURCE=0` selects the legacy
Nordic binary, for regression comparison only. The Nordic-binary path is the one
with a recorded hardware result for both NFC tap and approach unlock. The
in-tree stack is the default and is host and CI tested, and it still owes the
full phone checklist in
[`docs/hardware-validation.md`](docs/hardware-validation.md).

### ESP32

Needs ESP-IDF, esp-matter and a DWM3000EVB or DW3110. No NFC on any of them.

```bash
make esp-build APP=matter-lock TARGET=esp32s3   # or esp32c5, esp32c6
make esp-go    APP=matter-lock TARGET=esp32s3   # build + flash + monitor
```

ESP32-S3 has a recorded hardware result for approach unlock. The other targets
build and are released without one. Details in
[`ports/esp32/`](ports/esp32/) and [`docs/esp32-gotchas.md`](docs/esp32-gotchas.md).

## What it does

- **Unlock:** Home Key over BLE and UWB on approach, relock on departure, plus NFC Express Mode where the hardware has a reader.
- **Security:** credential-bound DS-TWR with STS, and a range consistency gate.
- **Speed:** credential reuse, PHY prewarm, 15 ms BLE intervals, fast auth.
- **Bare UWB:** the DW3110 runs CCC/FiRa, STS, DS-TWR and the M1 to M4 codec with no coprocessor.
- **Home Assistant:** UWB distance and access events over [MQTT](https://openaliro.github.io/openaliro/home-assistant.html), lock control over Matter, no firmware change. `make ha-setup HA=1`.

`make test` runs the host suite and needs a C compiler, not an SDK and not
hardware. `make verify` is the whole pre-push sweep. `make` with no target
prints every command, grouped.

<p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="assets/grid-demo-dark.webp"><source media="(prefers-color-scheme: light)" srcset="assets/grid-demo-light.webp"><img src="assets/grid-demo.webp" alt="Home Key setup, Approach Direction, provisioning, NFC tap, and lock-state notifications on live hardware"/></picture><br/><sub>Home Key · Approach Direction · provisioning · NFC tap · live lock state</sub></p>

## Trademarks and affiliation

This is an independent project. It is not affiliated with, endorsed by, sponsored by, or
speaking for any company or standards body named here.

Aliro and Matter are trademarks of the Connectivity Standards Alliance. Apple, iPhone and
Apple Watch are trademarks of Apple Inc. Nordic Semiconductor, Qorvo, DecaWave and
Espressif are trademarks of their respective owners. All are used here nominatively, to
say what this firmware interoperates with. All specifications, standards, trademarks and
other intellectual property referenced remain the property of their owners, along with
every right, licence and disclaimer attached to them.

Protocol notes in `docs/` cite specification section numbers so a reader can look them up
in their own copy. They do not reproduce specification text, and no member-confidential
material is included.

## Credits

Thanks: [@br101](https://github.com/br101) · [@kormax](https://github.com/kormax/) · [@rednblkx](https://github.com/rednblkx/) · [@scottjg](https://github.com/scottjg/).

---

<p align="center"><sub>License: ISC project code · mixed vendor terms · <a href="LICENSE">LICENSE</a> · <a href="PRIVACY.md">Privacy</a><br/>Independent project · No affiliation · No warranty · Do not secure valuables with it</sub></p>
