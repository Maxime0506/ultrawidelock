<!-- generated documentation — edit the source, not this file -->
# `modules/woz_nfc/src/pn532_bus.h`

Bus binding for the PN532 driver. One implementation is compiled in per
build (currently SPI: pn532_bus_spi.c). The transport uses only these
neutral names, so swapping the physical bus never touches pn532.c or
transport_pn532.cpp.

**depends on** [`modules/woz_nfc/src/pn532.h`](pn532.h.md)  ·  **used by** [`modules/woz_nfc/src/pn532_bus_spi.c`](pn532_bus_spi.c.md), [`modules/woz_nfc/src/transport_pn532.cpp`](transport_pn532.cpp.md)  ·  **discussed in** [`modules/woz_nfc/README.md`](../../../modules/woz_nfc/README.md)
