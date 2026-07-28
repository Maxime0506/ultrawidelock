<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/`

| subsystem | about |
|---|---|
| [`modules/woz_nfc/src/pn532.c`](pn532.c.md) | PN532 host-protocol driver. See pn532.h. OS-free: no Zephyr headers, no |
| [`modules/woz_nfc/src/pn532.h`](pn532.h.md) | NXP PN532 host-protocol driver: frame codec and the command subset needed by |
| [`modules/woz_nfc/src/pn532_apdu.c`](pn532_apdu.c.md) | @file pn532_apdu.c |
| [`modules/woz_nfc/src/pn532_apdu.h`](pn532_apdu.h.md) | PN532-specific ISO 7816 APDU adaptation. |
| [`modules/woz_nfc/src/pn532_bus.h`](pn532_bus.h.md) | Bus binding for the PN532 driver. One implementation is compiled in per |
| [`modules/woz_nfc/src/pn532_bus_spi.c`](pn532_bus_spi.c.md) | Zephyr SPI glue for the PN532 host protocol. |
| [`modules/woz_nfc/src/transport_none.cpp`](transport_none.cpp.md) | WozNfc backend for boards with no NFC frontend: polling never starts and no |
| [`modules/woz_nfc/src/transport_pn532.cpp`](transport_pn532.cpp.md) | WozNfc backend driving an NXP PN532 reader. |
| [`modules/woz_nfc/src/transport_rfal.cpp`](transport_rfal.cpp.md) | WozNfc backend forwarding to the add-on's ST25R/RFAL transport unchanged. |
