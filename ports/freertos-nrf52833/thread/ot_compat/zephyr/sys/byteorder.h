/* SPDX-License-Identifier: ISC */

/*
 * The byte-order helpers the pinned radio platform uses. nRF52833 is
 * little-endian, so the little-endian forms are plain copies and only the
 * memcpy swap actually reverses anything.
 */
#ifndef ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_BYTEORDER_H
#define ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_BYTEORDER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define sys_cpu_to_le16(x) ((uint16_t)(x))

static inline void sys_put_le16(uint16_t value, uint8_t dst[2])
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
}

static inline void sys_put_le64(uint64_t value, uint8_t dst[8])
{
	unsigned i;

	for (i = 0; i < 8u; i++) {
		dst[i] = (uint8_t)(value >> (8u * i));
	}
}

static inline void sys_memcpy_swap(void *dst, const void *src, size_t length)
{
	uint8_t *out = (uint8_t *)dst;
	const uint8_t *in = (const uint8_t *)src;
	size_t i;

	for (i = 0; i < length; i++) {
		out[i] = in[length - 1u - i];
	}
}

#endif /* ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_BYTEORDER_H */
