/* SPDX-License-Identifier: ISC */

/* Bus binding for the PN532 driver. One implementation is compiled in per
 * build (currently SPI: pn532_bus_spi.c). The transport uses only these
 * neutral names, so swapping the physical bus never touches pn532.c or
 * transport_pn532.cpp. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes shared by the bus backend and the portable driver. */
#define PN532_OK          0
#define PN532_ERR_IO      (-1)
#define PN532_ERR_TIMEOUT (-2)
#define PN532_ERR_FRAME   (-3)
#define PN532_ERR_APP     (-4)
#define PN532_ERR_STATUS  (-5)
#define PN532_ERR_SPACE   (-6)

/* Largest InDataExchange response host frame. */
#define PN532_FRAME_BUF_SIZE 275

/** Bus interface: host-frame write, readiness wait and frame read. */
struct pn532_bus_ops {
	int (*write)(void *ctx, const uint8_t *buf, size_t len);
	int (*wait_ready)(void *ctx, int timeout_ms);
	int (*read)(void *ctx, uint8_t *buf, size_t cap);
};

/* Bring up the bus device and any optional IRQ line. Returns 0 on success. */
int pn532_bus_init(void);

/* Bus operations table and context to hand to pn532_init(). */
extern const struct pn532_bus_ops pn532_bus_ops;
void *pn532_bus_ctx(void);

#ifdef __cplusplus
}
#endif
