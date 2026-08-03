/* dfufake: <zephyr/sys/crc.h>. REAL CRC-32, not a double.
 *
 * The whole point of this number is that two independently written programs
 * agree on it: scripts/woz_patch.py computes it with zlib.crc32 and the
 * bootloader recomputes it here. A fake would agree with itself and with
 * nothing else, so the IEEE polynomial is implemented properly and
 * crc32_ieee_update() chains across calls exactly as zlib's does. */
#ifndef DFUFAKE_ZEPHYR_SYS_CRC_H
#define DFUFAKE_ZEPHYR_SYS_CRC_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32_ieee(const uint8_t *data, size_t len);

#endif /* DFUFAKE_ZEPHYR_SYS_CRC_H */
