<h1 align="center">openaliro</h1>

<p align="center"><strong>Build an Aliro lock for iPhone and Apple Watch.</strong><br/>Hands-free over BLE + UWB · Express Mode over NFC · No app</p>

<p align="center"><a href="#start">Start</a> · <a href="#build">Build</a> · <a href="#features">Features</a> · <a href="#hardware">Hardware</a> · <a href="https://asxeem.github.io/openaliro/">Documentation ↗</a></p>

<p align="center"><a href="https://github.com/asxeem/openaliro/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/asxeem/openaliro/ci.yml?branch=main&style=flat-square&label=ci" alt="ci"/></a> <a href="https://github.com/asxeem/openaliro/releases"><img src="https://img.shields.io/github/v/release/asxeem/openaliro?style=flat-square" alt="latest release"/></a> <img src="https://img.shields.io/badge/license-source--available-lightgrey?style=flat-square" alt="source-available license"/></p>

<p align="center"><img src="assets/hero.gif" width="720" alt="A real iPhone unlocking openaliro on approach"/><br/><sub>Real hardware · Real Wallet key · Real approach unlock</sub></p>

<p align="center"><a href="web-twin/index.html"><kbd>Try web twin →</kbd></a>&nbsp;<a href="https://asxeem.github.io/openaliro/flash/"><kbd>Flash ESP32 →</kbd></a>&nbsp;<a href="https://github.com/asxeem/openaliro/releases/latest"><kbd>Latest release →</kbd></a>&nbsp;<a href="https://asxeem.github.io/openaliro/"><kbd>Documentation →</kbd></a></p>

<p align="center">Lock-side <a href="https://csa-iot.org/all-solutions/aliro/">Aliro</a> firmware · BLE auth · UWB ranging · proximity/NFC unlock</p>

## Start

- **Twin:** press **Walk up** to inspect every BLE/UWB step.
- **Browser flash:** Chrome/Edge · connect S3/C5 · **Install**.
- **Release:** follow the target `FLASH.md`.
- **Local test:** `make test` needs a C compiler, not an SDK or hardware.
- **Home Assistant:** `make ha-setup HA=1` wires the broker, agent, and config in one step.

## Build

```bash
git clone https://github.com/asxeem/openaliro.git
cd openaliro
```

Prefer a guided path? Install [Bun](https://bun.sh), then run `make openaliro` for a
walkthrough of bootstrap, build, flash, pairing, and diagnostics with a live serial
console. Details in [`tools/tui/`](tools/tui/README.md).

### nRF5340 DK

Primary hardware: nRF5340 DK + DWM3000EVB/DW3110 + X-NUCLEO-NFC12A1/ST25R300.

```bash
make bootstrap     # once; host tools + pinned NCS + workspace (~8.5 GB)
make build         # build/merged.hex
make flash-erase   # first flash
make term          # serial console
```

The in-tree Aliro stack is the default. Use `ALIRO_SOURCE=0` only for
legacy Nordic-binary comparison. Then use `make flash`.

### ESP32-S3 / ESP32-C5

Requires ESP-IDF, esp-matter, and a DWM3000EVB/DW3110.

```bash
cd ports/esp32/apps/matter-lock
make set-target TARGET=esp32s3   # or esp32c5; once per checkout
make go                          # build + flash + monitor
```

S3 is validated; C5 is build/release-tested.

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
- **Home Assistant:** UWB distance and access events over [MQTT](https://asxeem.github.io/openaliro/home-assistant.html), lock control over Matter; no firmware change.

<p align="center"><picture><source media="(prefers-color-scheme: dark)" srcset="assets/grid-demo-dark.webp"><source media="(prefers-color-scheme: light)" srcset="assets/grid-demo-light.webp"><img src="assets/grid-demo.webp" alt="Home Key setup, Approach Direction, provisioning, NFC tap, and lock-state notifications on live hardware"/></picture><br/><sub>Home Key · Approach Direction · provisioning · NFC tap · live lock state</sub></p>

## Hardware

- **nRF5340:** DK + DW3110 + ST25R300; released Nordic-binary path has NFC + approach validation; source-stack default is CI/host-tested pending the phone checklist.
- **ESP32-S3:** DW3110; release/source/browser image, approach validated.
- **ESP32-C5:** DW3110; release/source/browser image, build-tested.

## Credits

Thanks: [@br101](https://github.com/br101) · [@kormax](https://github.com/kormax/) · [@rednblkx](https://github.com/rednblkx/) · [@scottjg](https://github.com/scottjg/).

---

<p align="center"><sub>License: ISC project code · mixed vendor terms · <a href="LICENSE">LICENSE</a> · <a href="PRIVACY.md">Privacy</a><br/>Independent project · No affiliation · No warranty · Do not secure valuables with it</sub></p>
