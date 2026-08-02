<h1 align="center">openaliro</h1>

<p align="center"><strong>Build an Aliro lock for iPhone and Apple Watch.</strong><br/>Hands-free over BLE + UWB · Express Mode over NFC · No app</p>

<p align="center"><a href="#start">Start</a> · <a href="#build">Build</a> · <a href="#features">Features</a> · <a href="#hardware">Hardware</a> · <a href="https://openaliro.github.io/openaliro/">Documentation ↗</a></p>

<p align="center"><a href="https://github.com/openaliro/openaliro/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/openaliro/openaliro/ci.yml?branch=main&style=flat-square&label=ci" alt="ci"/></a> <a href="https://github.com/openaliro/openaliro/releases"><img src="https://img.shields.io/github/v/release/openaliro/openaliro?style=flat-square" alt="latest release"/></a> <img src="https://img.shields.io/badge/license-source--available-lightgrey?style=flat-square" alt="source-available license"/></p>

<p align="center"><img src="assets/hero.gif" width="720" alt="A real iPhone unlocking openaliro on approach"/><br/><sub>Real hardware · Real Wallet key · Real approach unlock</sub></p>

<p align="center"><a href="web-twin/index.html"><kbd>Try web twin →</kbd></a>&nbsp;<a href="https://openaliro.github.io/openaliro/flash/"><kbd>Flash ESP32 →</kbd></a>&nbsp;<a href="https://github.com/openaliro/openaliro/releases/latest"><kbd>Latest release →</kbd></a>&nbsp;<a href="https://openaliro.github.io/openaliro/"><kbd>Documentation →</kbd></a></p>

<p align="center">Lock-side <a href="https://csa-iot.org/all-solutions/aliro/">Aliro</a> firmware · BLE auth · UWB ranging · proximity/NFC unlock</p>

## Start

- **Twin:** press **Walk up** to inspect every BLE/UWB step.
- **Browser flash:** Chrome/Edge · connect S3/C5 · **Install**.
- **Release:** follow the target `FLASH.md`.
- **Local test:** `make test` needs a C compiler, not an SDK or hardware.
- **Home Assistant:** `make ha-setup HA=1` wires the broker, agent, and config in one step.

## Build

```bash
git clone https://github.com/openaliro/openaliro.git
cd openaliro
```

Prefer a guided path? Install [Bun](https://bun.sh), then run `make openaliro` for a
walkthrough of bootstrap, build, flash, pairing, and diagnostics with a live serial
console. Details in [`tools/tui/`](tools/tui/README.md).

### DWM3001CDK

One board, nothing else: a single nRF52833 carrying the Aliro reader, the
DW3110's ranging, a hand-written Matter node and an OpenThread MTD. Apple Home
commissions it over BLE and then shows a live lock tile. The bare targets mean
this board.

```bash
make bootstrap     # once; host tools + pinned NCS + workspace (~8.5 GB)
make build         # build/cdk-matter/merged.hex
make flash         # over the on-board J-Link OB
make monitor       # the console, over RTT — this board has no UART
```

`make reader` is the same source without Matter or Thread, which needs no
commissioner and is the quickest way to a working board. Details and the
bring-up log in [`firmware/README.md`](firmware/README.md).

### nRF5340 DK

The only target with NFC: nRF5340 DK + DWM3000EVB/DW3110 + X-NUCLEO-NFC12A1/ST25R300.

```bash
make bootstrap        # the same one-time setup as above
make nrf-build        # build/nrf5340dk/merged.hex
make nrf-flash-erase  # first flash
make nrf-term         # serial console
```

The in-tree Aliro stack is the default. Use `ALIRO_SOURCE=0` only for
legacy Nordic-binary comparison. Then use `make nrf-flash`.

### ESP32-S3 / ESP32-C5

Requires ESP-IDF, esp-matter, and a DWM3000EVB/DW3110.

```bash
make esp-set-target APP=matter-lock TARGET=esp32s3   # or esp32c5; once per target
make esp-go         APP=matter-lock                  # build + flash + monitor
```

The app directories still work as entry points — `cd ports/esp32/apps/matter-lock
&& make go` forwards to the same recipes. S3 is validated; C5 is build/release-tested.

### Add the key

1. Boot; copy the Matter QR URL or pairing code. ESP32: `codes`.
2. Add it in Apple Home; accept the test-certificate warning.
3. Wait for Home Key; walk up or tap NFC.

## Features

- **Unlock:** Home Key, BLE/UWB approach/relock, and NFC Express Mode.
- **Security:** credential-bound DS-TWR plus a consistency gate.
- **Validated:** released nRF Nordic-binary and ESP32-S3 approach paths; nRF NFC with ST25R300.
- **Speed:** credential reuse, PHY prewarm, 15 ms BLE, and fast auth.
- **Step-up:** Access Document verification and live advertisement tags.
- **Home controls:** single-antenna cosmetic direction; Matter user attribution.
- **Resilience:** state persistence, repair, envelope coalescing, and safe relock.
- **Bare UWB:** DW3110 runs CCC/FiRa, STS, DS-TWR, M1-M4; no coprocessor.
- **Power:** UWB sleeps out of range; ESP32 uses RSSI gating or predictive ETA.
- **Ports:** nRF5340, ESP32-S3, and ESP32-C5 share one engine.
- **Home Assistant:** UWB distance and access events over [MQTT](https://openaliro.github.io/openaliro/home-assistant.html), lock control over Matter; no firmware change.

<p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="assets/grid-demo-dark.webp"><source media="(prefers-color-scheme: light)" srcset="assets/grid-demo-light.webp"><img src="assets/grid-demo.webp" alt="Home Key setup, Approach Direction, provisioning, NFC tap, and lock-state notifications on live hardware"/></picture><br/><sub>Home Key · Approach Direction · provisioning · NFC tap · live lock state</sub></p>

## Hardware

- **DWM3001CDK:** one nRF52833 with the DW3110 on board; reader, Matter node and Thread MTD in a single image, approach unlock and a live Apple Home tile validated.
- **nRF5340:** DK + DW3110 + ST25R300; released Nordic-binary path has NFC + approach validation; source-stack default is CI/host-tested pending the phone checklist.
- **ESP32-S3:** DW3110; release/source/browser image, approach validated.
- **ESP32-C5:** DW3110; release/source/browser image, build-tested.

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
