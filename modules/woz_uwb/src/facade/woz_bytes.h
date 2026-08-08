/**
 * @file woz_bytes.h
 * Byte-order utilities: read/write 16-bit and 32-bit integers in little-endian or big-endian order.
 */
/*
 * woz_bytes.h - endian-neutral byte load/store helpers.
 *
 * Pure code: no OS, no allocation, no platform calls. It lives here rather than
 * behind a <zephyr/sys/byteorder.h> compat header because nothing about it is
 * platform-specific, so every port was re-supplying the same eight inlines.
 *
 * The Zephyr spellings (sys_get_le32 etc.) are kept deliberately: on Zephyr we
 * defer to the real header, so the names have to match, and keeping them means
 * the call sites need only swap an include.
 */
#ifndef WOZ_BYTES_H
#define WOZ_BYTES_H

#include <stdint.h>

#if defined(__ZEPHYR__)

/* Defer to Zephyr's own definitions. Redefining them here as static inlines
 * would collide wherever <zephyr/sys/byteorder.h> is pulled in transitively. */
#include <zephyr/sys/byteorder.h>

#else

/**
 * @brief Read a 16-bit little-endian value from a byte buffer.
 * @param p Pointer to the byte buffer.
 * @return The 16-bit value in native byte order.
 */
static inline uint16_t sys_get_le16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/**
 * @brief Read a 32-bit little-endian value from a byte buffer.
 * @param p Pointer to the byte buffer.
 * @return The 32-bit value in native byte order.
 */
static inline uint32_t sys_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/**
 * @brief Read a 16-bit big-endian value from a byte buffer.
 * @param p Pointer to the byte buffer.
 * @return The 16-bit value in native byte order.
 */
static inline uint16_t sys_get_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief Read a 32-bit big-endian value from a byte buffer.
 * @param p Pointer to the byte buffer.
 * @return The 32-bit value in native byte order.
 */
static inline uint32_t sys_get_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

/**
 * @brief Write a 16-bit value to a byte buffer in little-endian order.
 * @param v The value to write.
 * @param p Pointer to the byte buffer.
 */
static inline void sys_put_le16(uint16_t v, uint8_t *p)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief Write a 32-bit value to a byte buffer in little-endian order.
 * @param v The value to write.
 * @param p Pointer to the byte buffer.
 */
static inline void sys_put_le32(uint32_t v, uint8_t *p)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/**
 * @brief Write a 16-bit value to a byte buffer in big-endian order.
 * @param v The value to write.
 * @param p Pointer to the byte buffer.
 */
static inline void sys_put_be16(uint16_t v, uint8_t *p)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

/**
 * @brief Write a 32-bit value to a byte buffer in big-endian order.
 * @param v The value to write.
 * @param p Pointer to the byte buffer.
 */
static inline void sys_put_be32(uint32_t v, uint8_t *p)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

#endif /* __ZEPHYR__ */

#endif /* WOZ_BYTES_H */
