/* Bus binding for the PN532 driver. One implementation is compiled in per
 * build (currently SPI: pn532_bus_spi.c). The transport uses only these
 * neutral names, so swapping the physical bus never touches pn532.c or
 * transport_pn532.cpp. */
#pragma once

#include "pn532.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the bus device and any optional IRQ line. Returns 0 on success. */
int pn532_bus_init(void);

/* Bus operations table and context to hand to pn532_init(). */
extern const struct pn532_bus_ops pn532_bus_ops;
void *pn532_bus_ctx(void);

#ifdef __cplusplus
}
#endif
