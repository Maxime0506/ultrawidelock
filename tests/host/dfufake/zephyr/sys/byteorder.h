/* dfufake: <zephyr/sys/byteorder.h>, the little-endian accessors woz_dfu uses.
 * Byte-wise rather than a cast, so they are correct on either host endianness
 * and never depend on the buffer being aligned. */
#ifndef DFUFAKE_ZEPHYR_SYS_BYTEORDER_H
#define DFUFAKE_ZEPHYR_SYS_BYTEORDER_H

#include <stdint.h>

static inline void sys_put_le16(uint16_t value, uint8_t dst[2])
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
}

static inline void sys_put_le32(uint32_t value, uint8_t dst[4])
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
	dst[2] = (uint8_t)(value >> 16);
	dst[3] = (uint8_t)(value >> 24);
}

static inline uint16_t sys_get_le16(const uint8_t src[2])
{
	return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static inline uint32_t sys_get_le32(const uint8_t src[4])
{
	return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	       ((uint32_t)src[3] << 24);
}

#endif /* DFUFAKE_ZEPHYR_SYS_BYTEORDER_H */
