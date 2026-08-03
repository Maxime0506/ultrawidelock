<a name="readme-top"></a>

<div align="center">

<a href="https://openaliro.github.io/openaliro/"><img src="assets/social-preview.png" width="880" alt="openaliro — an Aliro digital key lock: iPhone or Apple Watch unlocks it on approach (UWB) or on tap (NFC)"/></a>

<h1>openaliro</h1>

**Build an Aliro lock your iPhone opens by walking up to it.**

Hands-free over BLE + UWB · one board · no app · no cloud · no vendor Aliro binary

<p>
<a href="https://github.com/openaliro/openaliro/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/openaliro/openaliro/ci.yml?branch=main&style=flat-square&label=ci&labelColor=0D1117&logo=githubactions&logoColor=white" alt="ci"/></a>
<a href="https://github.com/openaliro/openaliro/releases"><img src="https://img.shields.io/github/v/release/openaliro/openaliro?style=flat-square&labelColor=0D1117&color=2ea043&logo=github&logoColor=white" alt="latest release"/></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/license-source--available-lightgrey?style=flat-square&labelColor=0D1117" alt="source-available license"/></a>
<a href="CHANGELOG.md"><img src="https://img.shields.io/badge/changelog-read-8957e5?style=flat-square&labelColor=0D1117" alt="changelog"/></a>
<a href="https://discord.gg/7Ez9SRD87Q"><img src="https://img.shields.io/badge/discord-join-5865F2?style=flat-square&labelColor=0D1117&logo=discord&logoColor=white" alt="discord"/></a>
</p>

<p>
<img src="https://img.shields.io/badge/Apple%20Home-commissions%20it-000000?style=flat-square&labelColor=0D1117&logo=apple&logoColor=white" alt="Apple Home"/>
<img src="https://img.shields.io/badge/Matter-hand--written%20node-3f8cff?style=flat-square&labelColor=0D1117" alt="Matter"/>
<img src="https://img.shields.io/badge/Thread-OpenThread%20MTD-8a2be2?style=flat-square&labelColor=0D1117" alt="Thread"/>
<img src="https://img.shields.io/badge/Bluetooth-Aliro%200xFFF2-0082FC?style=flat-square&labelColor=0D1117&logo=bluetooth&logoColor=white" alt="Bluetooth"/>
<img src="https://img.shields.io/badge/UWB-DW3110%20·%20no%20coprocessor-ff7a18?style=flat-square&labelColor=0D1117" alt="UWB"/>
<img src="https://img.shields.io/badge/nRF52833-512%20KB%20flash%20·%20128%20KB%20RAM-00A9CE?style=flat-square&labelColor=0D1117&logo=nordicsemiconductor&logoColor=white" alt="nRF52833"/>
</p>

<p>
<a href="#-build-it"><kbd> &nbsp; Build it &nbsp; </kbd></a>&nbsp;
<a href="firmware/README.md"><kbd> &nbsp; The board &nbsp; </kbd></a>&nbsp;
<a href="https://openaliro.github.io/openaliro/flash/"><kbd> &nbsp; Flash an ESP32 &nbsp; </kbd></a>&nbsp;
<a href="web-twin/index.html"><kbd> &nbsp; Web twin &nbsp; </kbd></a>&nbsp;
<a href="https://openaliro.github.io/openaliro/"><kbd> &nbsp; Documentation &nbsp; </kbd></a>&nbsp;
<a href="https://discord.gg/7Ez9SRD87Q"><kbd> &nbsp; Discord &nbsp; </kbd></a>
</p>

<img src="assets/hero.gif" width="760" alt="A real iPhone unlocking openaliro on approach"/>

<sub>Real hardware · a real Wallet key · a real walk-up unlock. Not a mock-up.</sub>

</div>

<br/>

> **The short version.** Lock-side [Aliro][aliro] firmware. Your iPhone or Apple Watch
> holds a Wallet home key; the lock advertises over Bluetooth, authenticates the key,
> then measures the *actual distance* to the phone over ultra-wideband and opens when
> you are genuinely there. It commissions into Apple Home like any other accessory,
> and it runs on one 512 KB microcontroller that was not supposed to fit any of this.

<br/>

<details>
<summary><kbd> &nbsp; Table of contents &nbsp; </kbd></summary>

#### TOC

- [How a door opens](#how-a-door-opens)
- [The board](#-the-board)
  - [What has actually run on hardware](#what-has-actually-run-on-hardware)
  - [There is no NFC tap on this board](#there-is-no-nfc-tap-on-this-board)
- [Build it](#-build-it)
- [Update it over the air](#-update-it-over-the-air)
- [Things worth knowing before you rely on it](#-things-worth-knowing-before-you-rely-on-it)
- [The other two targets](#-the-other-two-targets)
- [What it does](#-what-it-does)
- [Under the hood](#-under-the-hood)
- [The command deck](#-the-command-deck)
- [Where the documentation is](#-where-the-documentation-is)
- [Come and talk about it](#-come-and-talk-about-it)
- [Trademarks and affiliation](#trademarks-and-affiliation)
- [Credits](#credits)

<br/>

</details>

## How a door opens

```text
    iPhone / Apple Watch               openaliro on one board
   ┌────────────────────┐             ┌──────────────────────┐
   │  Wallet home key   │             │  nRF52833 + DW3110   │
   └──────────┬─────────┘             └───────────┬──────────┘
              │                                   │
   1  BLE     │  advert 0xFFF2, 15 ms intervals   │
              │ ─────────────────────────────────▶│
   2  Aliro   │  AUTH0 → AUTH1 → EXCHANGE         │
              │ ◀────────────────────────────────▶│
              │  both ends now hold the URSK      │
   3  UWB     │  CCC key ladder → STS → DS-TWR    │
              │ ◀──────── 565 cm → 0 cm ─────────▶│
   4  Gate    │  range consistency agrees         │
              │      ──  U N L O C K  ──          │
   5  Matter  │  lock state over Thread           └──▶ Apple Home
              ╵
```

The unlock path is local: no app to install, no account, no cloud round trip. Step 3 is
the design's whole point — the distance is *measured*, using a scrambled timestamp
sequence bound to the credential, rather than asserted by whoever is talking.

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 📟 The board

The headline target is a **Qorvo DWM3001CDK**. One nRF52833 (512 KB of flash,
128 KB of RAM), a DW3110 radio beside it inside the same DWM3001C module, and a J-Link
already on the board. Nothing to wire, nothing to solder, no second development kit,
no ribbon cable.

That single part carries all of this at once:

| | Running on the same nRF52833 |
|:--|:--|
| 📶 | the **BLE peripheral** an iPhone approaches and talks Aliro to |
| 🔑 | the **Aliro reader engine**: AUTH0 / AUTH1 / EXCHANGE, the CCC key ladder, STS, DS-TWR |
| 🏠 | a **hand-written Matter node** ([`modules/woz_matter`](modules/woz_matter/)), not CHIP |
| 🧵 | an **OpenThread MTD**, so the lock joins a real Thread network |
| 📏 | the **DW3110's UWB ranging**, over the module's internal SPI |

<table>
<tr><td width="34" align="center" valign="top">🧨</td><td>

**Putting Matter on this part is the piece that was supposed to be impossible.**

Nordic's most stripped *supported* single-core CHIP Matter lock — LTO on, no shell, no
console, no logging — measures **614,008 B of flash and 162,164 B of RAM**:

| | CHIP lock needs | This board has | Over by |
|:--|--:|--:|--:|
| Flash | 614,008 B | 516,096 B | **95.6 KB** |
| RAM | 162,164 B | 131,072 B | **30.4 KB** |

That is before a single line of reader or UWB code joins it, and the nRF52833 has no
QSPI, so there is no external memory to escape into. Writing the Matter node by hand is
what closed the gap. Apple Home commissions this board over BLE and then shows a live
lock tile, on the part that could not hold the stack it was meant to need.

</td></tr>
</table>

### What has actually run on hardware

| | Milestone | State |
|:--:|:--|:--|
| ✅ | Reader, Matter node and Thread MTD in one image | fits: **409,988 B** flash, **125,012 B** RAM, LTO on by default |
| ✅ | iPhone enumerates the Aliro `0xFFF2` service over BLE | done |
| ✅ | DW3110 answers, live ranging against an iPhone | done, 565 cm down to 0 cm |
| ✅ | An initiator reaches `ESTABLISHED` | done, the iPhone itself |
| 🏆 | **iPhone Wallet walk-up unlock** | **done**, four in one session, 2026-08-02 |
| ✅ | Firmware update over Bluetooth | done, byte-identical result, 2026-08-03 |
| ⬜ | **≥ 95% ranging success over 100 walk-ups** | **open, never run** |

Everything above that last row has been demonstrated on real hardware. Nothing has yet
been demonstrated *at a rate*: the walk-up sample so far is single digits, and closing it
needs someone to walk at a door a hundred times. Per-stage evidence, with the log lines,
is in [`firmware/README.md`](firmware/README.md).

### There is no NFC tap on this board

That is a hardware fact rather than a missing feature. The DWM3001CDK carries no NFC
reader IC, and the nRF52833's own NFC peripheral is tag emulation only, so the part can
be *read* but cannot *read*. BLE plus UWB walk-up is the whole feature set here. If you
want Express Mode over NFC, [the nRF5340 DK](#-the-other-two-targets) is the target that
has it.

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 🔨 Build it

**Five commands from an empty machine to a lock on your door.**

```bash
git clone https://github.com/openaliro/openaliro.git
cd openaliro

make dfu-key      # 1 · once per clone   ·  this checkout's image-signing key
make bootstrap    # 2 · once per machine ·  host tools + pinned NCS v3.3.0 (~8.5 GB)
make build        # 3 · the lock         ->  build/cdk-matter
make flash        # 4 · over the on-board J-Link, no external probe
make monitor      # 5 · the console, over RTT
```

<table>
<tr><td width="34" align="center" valign="top">🔑</td><td>

**Do not skip step 1.** Every image on this board is signed, and with no key the build
**fails at configure** rather than quietly falling back to the demo key published in
MCUboot's own repository. The key is gitignored, so a fresh clone or a new git worktree
needs its own.

</td></tr>
</table>

Bare targets always mean **this board and its Matter image**:

| Command | Builds | Output |
|:--|:--|:--|
| `make build` | reader + Matter node + Thread MTD | `build/cdk-matter` |
| `make rebuild` | the same, forced pristine | `build/cdk-matter` |
| `make reader` | Aliro + UWB only, no Matter, no Thread | `build/cdk-reader` |
| `make selftest` | one-shot DW3110 `DEV_ID` read over SPI at boot | `build/cdk-selftest` |
| `make flash` · `flash-erase` · `monitor` | write it, wipe-and-write it, watch it | — |

Options are make variables — `PRISTINE=1`, `RELEASE=1`, `LTO=0`, `SMP=1` — and `make`
with no target prints every one of them, grouped.

<table>
<tr><td width="34" align="center" valign="top">🔢</td><td>

**The setup code comes from the build, not from the board.** `make build` and
`make monitor` each end by printing the line Apple Home is about to ask you for, because
a Matter device stores the SPAKE2+ *verifier* and never the passcode — so nothing on the
device can print it. The code is re-derived on the host from the configuration the build
just produced, and checked against the verifier that was compiled in. Add the accessory
with **More options… → Enter Code**: there is no QR label on this board.

</td></tr>
</table>

<details>
<summary><kbd> &nbsp; Just the radio &nbsp; </kbd> &nbsp; <b><code>make reader</code></b> — the same source without Matter or Thread</summary>

<br/>

Aliro and UWB only, no commissioner and no Thread network required, with the reader
identity typed in over USB. It is the fastest route to a working board and the right one
for bench work on the radio. It builds elsewhere on purpose, so the flash and console
targets keep meaning the Matter image:

```bash
make reader
make flash   CDK_BUILD=build/cdk-reader
make monitor CDK_RTT_BUILD=build/cdk-reader
```

`make selftest` is the bring-up image: it reads the DW3110's `DEV_ID` over SPI at boot,
which is how a wrong pin, a wrong SPI mode or an unpowered radio tells you so in one line.

</details>

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 📡 Update it over the air

The board updates itself over Bluetooth, with no cable and no probe. Proven on hardware
on 2026-08-03: a patch went over the air and the board's flash came out byte for byte
identical to the target image, with matching CRCs on both sides.

Two full MCUboot slots want about **844 KB on a 512 KB part**, and there is no external
flash to stage into, so what travels is a signed **delta**.

```text
   host                                board
   ───────────────────────             ──────────────────────────
   woz_patch.py                        patch_staging partition
   diff + sign the delta   ──GATT──▶   staged by the application
                                       │
                                       ▼   reboot
                                       woz_dfu, from a SYS_INIT
                                       INSIDE MCUboot   (17–31 s)
                                       │
                                       ▼
                                       MCUboot re-verifies, boots
```

[`scripts/woz_patch.py`](scripts/woz_patch.py) builds and signs it,
[`scripts/woz_push.py`](scripts/woz_push.py) carries it over GATT, the application stages
it, and [`modules/woz_dfu`](modules/woz_dfu/) applies it from inside the bootloader,
because an application cannot rewrite the flash it is executing from.

```bash
make dfu         # build, diff against what the board runs, sign, push. One command.
make fota        # instead: the single file a phone can install, plus the steps
make fota-done   # after a phone push, confirm what the board is now running
```

An update needs an open window, and **the window is the whole authorization model**. The
patch is signed and MCUboot re-verifies the result before booting it, so no peer can
install code either way; what a closed window prevents is a stranger in radio range
spending your flash's erase cycles. Two ways to open it, both confirmed on a live
commissioned lock:

| Open the window | How |
|:--|:--|
| 🔘 **At the board** | Press **SW2**. |
| 🏠 **From Apple Home** | **"Turn On Pairing Mode"** — the node serves the AdministratorCommissioning cluster. |
| 🔌 **From the host** | `make ota-window`, over SWD, when you would rather not walk to the door. |

The board says so itself while the window is open, on its blue LED, so a press that did
not register is visible without a debugger attached.

<table>
<tr><td width="34" align="center" valign="top">🛑</td><td>

**`make fota-done` is not optional after a push from a phone.** A delta is computed
against the exact bytes on the board, only the build host keeps that record, and a push
from the phone is invisible to it. Skip it and the *next* update is built from the wrong
base and refused.

</td></tr>
</table>

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## ⚠️ Things worth knowing before you rely on it

<table>
<tr><td width="34" align="center" valign="top">🖥️</td><td>

**The console is RTT, over `probe-rs`, not UART.** There is no UART console on this
board: on a single-core part the DW3110's delayed-transmit reply window cannot afford a
blocking console write. `make monitor` attaches with the ELF, which must be the one you
*flashed*. The RTT ring survives a reset on purpose, so the first block you see is the
previous run — anchor on the `*** Booting nRF Connect SDK ***` line.

</td></tr>
<tr><td width="34" align="center" valign="top">🧹</td><td>

**`make flash-erase` costs the commissioning.** It takes the Matter fabrics, the reader
identity and its trust anchors, so Apple Home has to add the lock again. To clear only
what a controller can see, hold **SW2 through reset** instead: same effect on the fabrics
and the anchors, and it leaves the Thread settings alone.

</td></tr>
<tr><td width="34" align="center" valign="top">🔒</td><td>

**APPROTECT must never be locked on this board.** Two independent guards fail the build
if it is. Recovering debug access costs a mass erase of flash *and* UICR, which takes the
reader's private key and every iPhone key ever provisioned against it.

</td></tr>
<tr><td width="34" align="center" valign="top">🚧</td><td>

**Nothing revokes yet, and the trust store holds four phone keys.** A phone removed in
Apple Home still opens the board until the store is cleared. Read
[`firmware/README.md`](firmware/README.md) before trusting it with anything, and do not
secure valuables with it.

</td></tr>
</table>

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 🧩 The other two targets

Both are still here, both still build, and both share the same engine in
[`modules/`](modules/README.md). Neither is the headline any more.

| Target | Unlock on approach | NFC tap | Matter | Build prefix |
|:--|:--:|:--:|:--:|:--|
| **DWM3001CDK** — the headline | ✅ validated | ❌ no reader IC | ✅ in-tree node | *(bare)* `make build` |
| **nRF5340 DK** — the one with NFC | ✅ validated | ✅ Express Mode | ✅ CHIP add-on | `make nrf-build` |
| **ESP32-S3** | ✅ validated | ❌ | ✅ esp-matter | `make esp-build` |
| **ESP32-C5 / C6** | ⬜ builds, no recorded run | ❌ | ✅ esp-matter | `make esp-build` |

<details>
<summary><kbd> &nbsp; nRF5340 DK &nbsp; </kbd> &nbsp; <b>the one with NFC</b></summary>

<br/>

The only target with an Express Mode tap. It wants an nRF5340 DK plus a DWM3000EVB or
DW3110 plus an X-NUCLEO-NFC12A1 or ST25R300, and the wiring in
[`docs/nrf5340-wiring.md`](docs/nrf5340-wiring.md).

```bash
make bootstrap        # the same one-time setup
make dfu-key          # the same key as the CDK, skip it if you already ran it
make nrf-build        # -> build/nrf5340dk/merged.hex
make nrf-flash-erase  # the first flash
make nrf-term         # serial console
```

This board is signed for the same reason the CDK is: MCUboot plus Matter OTA (`DFU=1`) is
its default too, and the build refuses to hand the bootloader a key this checkout does not
own. One key covers both boards. `DFU=0` builds the older no-bootloader bench layout,
which needs no key.

The in-tree Aliro stack is the default; `ALIRO_SOURCE=0` selects the legacy Nordic binary,
for regression comparison only. The Nordic-binary path is the one with a recorded hardware
result for both NFC tap and approach unlock. The in-tree stack is the default and is host
and CI tested, and it still owes the full phone checklist in
[`docs/hardware-validation.md`](docs/hardware-validation.md).

</details>

<details>
<summary><kbd> &nbsp; ESP32 &nbsp; </kbd> &nbsp; <b>S3, C5, C6</b></summary>

<br/>

Needs ESP-IDF, esp-matter and a DWM3000EVB or DW3110. No NFC on any of them.

```bash
make esp-build APP=matter-lock TARGET=esp32s3   # or esp32c5, esp32c6
make esp-go    APP=matter-lock TARGET=esp32s3   # build + flash + monitor
```

ESP32-S3 has a recorded hardware result for approach unlock. The other targets build and
are released without one. Details in [`ports/esp32/`](ports/esp32/) and
[`docs/esp32-gotchas.md`](docs/esp32-gotchas.md).

There is also a browser flasher, if you would rather not install anything:
[**Flash an ESP32 from this page ↗**](https://openaliro.github.io/openaliro/flash/)

</details>

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## ✨ What it does

|  | |
|:--|:--|
| 🚪 **Unlock** | Home Key over BLE and UWB on approach, relock on departure, plus NFC Express Mode where the hardware has a reader. |
| 🛡️ **Security** | Credential-bound DS-TWR with STS, and a [range consistency gate](docs/range-integrity.md) — proximity is measured, not claimed. |
| ⚡ **Speed** | Credential reuse, PHY prewarm, 15 ms BLE intervals, fast auth. |
| 📻 **Bare UWB** | The DW3110 runs CCC/FiRa, STS, DS-TWR and the M1–M4 codec with **no UWB coprocessor**. |
| 🧭 **Approach Direction** | Apple Home's own control is exposed, and its Left / Front / Right selection round-trips to the device — [`docs/approach-direction.md`](docs/approach-direction.md). |
| 🏡 **Home Assistant** | UWB distance and access events over [MQTT][ha-docs], lock control over Matter, no firmware change. `make ha-setup HA=1`. |
| 🧪 **Testable on a laptop** | `make test` runs the host suite with a C compiler — no SDK, no hardware. `make verify` is the whole pre-push sweep. |

<div align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/grid-demo-dark.webp">
  <source media="(prefers-color-scheme: light)" srcset="assets/grid-demo-light.webp">
  <img src="assets/grid-demo.webp" width="880" alt="Home Key setup, Approach Direction, provisioning, NFC tap, and lock-state notifications on live hardware"/>
</picture>
<br/>
<sub>Home Key · Approach Direction · provisioning · NFC tap · live lock state</sub>
</div>

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 🔬 Under the hood

The Aliro stack here is this project's own source, not a vendor binary. On the primary
board the Matter node is this project's own source too. Every module below is compiled by
all three targets, from one tree — the full table with per-module portability lives in
[`modules/README.md`](modules/README.md).

| Module | What it is |
|:--|:--|
| [`woz_port`](modules/woz_port/) | The platform contract: heap, clock, sleeps, mutex, logging. A new RTOS is a new branch in two headers. |
| [`woz_uwb`](modules/woz_uwb/) | The UWB engine, one Kconfig tier per layer: bare DW3000 bring-up → DS-TWR responder → the CCC credential-bound STS engine and the M1–M4 codec. |
| [`woz_aliro`](modules/woz_aliro/) | The portable C reader: key schedule, secure channel, wire codec, provisioning, ranging glue, RSSI gate, approach controller. |
| [`woz_matter`](modules/woz_matter/) | The hand-written Matter node: TLV, message and exchange layers, MRP, BTP, PASE/CASE, attestation, clusters. Exists because CHIP does not fit. |
| [`woz_dfu`](modules/woz_dfu/) | Delta firmware update over Bluetooth: a receiver in the app, an applier inside MCUboot. |
| [`woz_aliro_stack`](modules/woz_aliro_stack/) | The Nordic Aliro API reimplemented from the published Aliro 1.0 spec — and a build that rejects any link map the vendor binary contributed to. |
| [`woz_nfc`](modules/woz_nfc/) · [`woz_aliro_ecp`](modules/woz_aliro_ecp/) | The NFC transport seam (RFAL / PN532 / none) and the ECP emitter for Express tap. nRF5340 only. |

Around them: [`tests/host/`](tests/host/) (KAT suite plus CBMC proofs and fuzz harnesses),
[`security/`](security/) (the eight blocking gates), [`web-twin/`](web-twin/) (the
firmware compiled to WASM so the protocol runs in a browser),
[`integration/homeassistant/`](integration/homeassistant/),
[`host/presence/`](host/presence/) and [`tools/`](tools/) (docs pipeline plus the
`make openaliro` bench TUI).

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 🎛️ The command deck

`make` with no target prints every target, grouped, and each recipe's `##` block in
[`mk/`](mk/) is the authority on its options. The ones you will actually type:

| | |
|:--|:--|
| `make bootstrap` | Set this machine up. The only command before `build`. |
| `make dfu-key` | This checkout's MCUboot signing key. Once per clone. |
| `make build` · `flash` · `monitor` | The DWM3001CDK lock: compile, write, watch. |
| `make reader` · `selftest` | The radio without Matter; and the one-shot SPI bring-up check. |
| `make dfu` · `fota` · `fota-done` | Update over Bluetooth; make the file a phone installs; record what landed. |
| `make test` · `check` · `verify` | Host suite; every host suite under one banner; the full pre-push sweep. |
| `make security` | The eight blocking security gates — what a PR must pass. |
| `make fuzz` · `cbmc` · `coverage` | Parser hardening, bounded model checking, line coverage. |
| `make docs` | Rebuild the documentation site → `site/index.html`. |
| `make openaliro` | The guided bench TUI. |
| `make ws-seed` | Give this git worktree its own workspace (APFS copy-on-write, ~0 disk). |

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 📚 Where the documentation is

**[openaliro.github.io/openaliro ↗][site]** is the rendered site. In the tree:

| Read this | When |
|:--|:--|
| [`firmware/README.md`](firmware/README.md) | The DWM3001CDK manual: sizes, partitions, RTT, OTA, provisioning, per-stage evidence. |
| [`docs/set-up.md`](docs/set-up.md) | First bring-up, from an empty machine. |
| [`docs/configuring.md`](docs/configuring.md) | Every build option, per target. |
| [`docs/troubleshooting.md`](docs/troubleshooting.md) | Symptoms, grouped by target, CDK first. |
| [`docs/dwm3001cdk-surgery.md`](docs/dwm3001cdk-surgery.md) | The CDK Matter-node traps, each with symptom and fix. |
| [`docs/esp32-gotchas.md`](docs/esp32-gotchas.md) | The same, numbered, for ESP32. |
| [`docs/protocol-notes.md`](docs/protocol-notes.md) · [`protocol-research.md`](docs/protocol-research.md) | Time sync, credential validity, and the wider reverse-engineering notes. |
| [`docs/range-integrity.md`](docs/range-integrity.md) · [`approach-direction.md`](docs/approach-direction.md) | Why a measured distance is trustworthy, and how direction is honoured. |
| [`docs/hardware-validation.md`](docs/hardware-validation.md) | The phone checklist each target still owes. |
| [`docs/home-assistant.md`](docs/home-assistant.md) · [`power-profile.md`](docs/power-profile.md) | Integration, and where the milliamps go. |
| [`docs/README.md`](docs/README.md) → [`docs/architecture/`](docs/architecture/) | Generated code map: subsystems, entry points, every exported symbol. |

New here? [`CONTRIBUTING.md`](CONTRIBUTING.md) · [`SECURITY.md`](SECURITY.md) ·
[`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) · [`PRIVACY.md`](PRIVACY.md)

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

## 💬 Come and talk about it

<div align="center">

<a href="https://discord.gg/7Ez9SRD87Q"><img src="https://img.shields.io/badge/JOIN%20THE%20DISCORD-5865F2?style=for-the-badge&labelColor=0D1117&logo=discord&logoColor=white" alt="Join the openaliro Discord"/></a>

<sub>Bring-up questions · walk-ups that did not work · boards you want ported · the 100-walk-up run</sub>

</div>

| Where | For |
|:--|:--|
| 💬 [**Discord**](https://discord.gg/7Ez9SRD87Q) | Live bring-up help, RTT logs that make no sense, and "did anyone get this board working". |
| 🐛 [**Issues**](https://github.com/openaliro/openaliro/issues) | Reproducible bugs, with the target, the build command and the console output. |
| 🤝 [**CONTRIBUTING.md**](CONTRIBUTING.md) | What `make verify` expects from a patch before it can land. |
| 🔐 [**SECURITY.md**](SECURITY.md) | Anything that would let the wrong phone open a door — privately, not in Discord. |

<div align="right"><sub><a href="#readme-top">↑ back to top</a></sub></div>

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

Thanks: [@br101](https://github.com/br101) · [@kormax](https://github.com/kormax/) ·
[@rednblkx](https://github.com/rednblkx/) · [@scottjg](https://github.com/scottjg/).

---

<div align="center">

<sub>License: ISC project code · mixed vendor terms · <a href="LICENSE">LICENSE</a> · <a href="PRIVACY.md">Privacy</a></sub>

<sub><b>Independent project · no affiliation · no warranty · do not secure valuables with it</b></sub>

<br/>

<a href="#readme-top"><img src="https://img.shields.io/badge/↑_back_to_the_top-0D1117?style=flat-square" alt="back to top"/></a>

</div>

[aliro]: https://csa-iot.org/all-solutions/aliro/
[site]: https://openaliro.github.io/openaliro/
[ha-docs]: https://openaliro.github.io/openaliro/home-assistant.html
