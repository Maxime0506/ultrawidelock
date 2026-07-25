# Woz NFC transport seam

The add-on application's five NFC call sites (`Init`/`Start`/`Stop` in
`init.cpp`, `Send`/`Terminate` in `interface_impl/session.cpp`) are patched by
`integration/patches/nfc-transport-seam.patch` to call `WozNfc::*` instead of
the concrete `NfcTransportRfal` singleton. This module supplies the selected
backend:

| `WOZ_NFC_TRANSPORT_…` | Backend | Hardware |
| --- | --- | --- |
| `RFAL` (default when `NFC_DRIVER_STM=y`) | forwards to upstream `NfcTransportRfal` | X-NUCLEO ST25R500 shield on `spi1` |
| `PN532` | in-tree PN532 transport | PN532 breakout on `spi1` — the bus the ST25R shield would use (see `integration/overlays/pn532.overlay`) |
| `NONE` (default otherwise) | no-op | none — Aliro NFC flow disabled, BLE/UWB unaffected |

Build selection from the repo root:

```sh
make build              # default: st25r — upstream ST25R/RFAL path
make build NFC=pn532    # PN532 breakout on spi1
make build NFC=none     # no reader fitted — Aliro NFC flow disabled
```

## PN532 backend

- `pn532.[ch]` — host-protocol driver (frames, ACK, command subset, ISO-DEP
  chaining via the MI bit). OS-free with injected bus ops; unit-tested on the
  host against a scripted fake bus (`tests/host/test_pn532.c`).
- `pn532_bus.h` + `pn532_bus_spi.c` — Zephyr SPI glue for devicetree
  `"nxp,pn532-spi"` (mode 0, LSB-first via `SPI_TRANSFER_LSB`; the neutral
  `pn532_bus.h` interface keeps the transport bus-agnostic). Ready-status is
  polled with the STATREAD command unless `irq-gpios` is wired (active low; the
  pad is driven low when a frame is pending).
- `transport_pn532.cpp` — polling thread and Aliro session lifecycle. Each
  discovery round raises the RF field, broadcasts one Apple ECP v2 frame
  (same layout as `modules/woz_aliro_ecp`, CRC engines gated off around a raw
  `InCommunicateThru`), attempts one 106 kbps type A activation, then drops
  the field. Stack callbacks are posted to the Aliro workqueue to match the
  RFAL transport's threading contract.

The PN532 sits on `spi1`, the same nRF5340 serial box the ST25R shield uses
(and which `i2c1` shares — they cannot both be enabled). The overlay disables
the ST25R child and gives its chip-select to the PN532, so no `NFC=st25r` and
`NFC=pn532` build ever contend for the bus.

Practical notes: the PN532 services ISO-DEP WTX autonomously, so slow
User-Device operations (user auth during AUTH1, Access Document signing)
are bounded by `WOZ_NFC_PN532_EXCHANGE_TIMEOUT_MS`, not by the chip's
per-frame wait. Multi-KB step-up transfers ride the MI-bit chaining in
`pn532_in_data_exchange()`.
