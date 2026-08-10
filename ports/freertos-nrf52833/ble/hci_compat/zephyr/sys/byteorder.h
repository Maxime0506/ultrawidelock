/*
 * Zephyr-shaped shim, not Zephyr code.
 *
 * The pinned dispatcher reads little-endian opcodes out of the HCI command
 * header with this one accessor.
 */
#ifndef WOZ_HCI_COMPAT_SYS_BYTEORDER_H
#define WOZ_HCI_COMPAT_SYS_BYTEORDER_H

#include <stdint.h>

static inline uint16_t sys_get_le16(const uint8_t src[2])
{
	return ((uint16_t)src[1] << 8) | src[0];
}

#endif /* WOZ_HCI_COMPAT_SYS_BYTEORDER_H */
