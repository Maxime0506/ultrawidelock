/*
 * The woz_flash contract on this port: the two partitions woz_dfu touches.
 *
 * Thin on purpose. board/flash_freertos.c already does the hard part -- it
 * arbitrates NVMC against the radio with MPSL timeslots, because erasing a page
 * stalls the CPU for milliseconds and a radio event that lands inside that stall
 * is a dropped connection. What is here is only the partition bookkeeping the
 * contract asks for: which window of flash an id names, and bounds checks that
 * make a write outside it a returned error rather than a corrupted neighbour.
 *
 * The two windows come from the linker script, not from constants here. That is
 * the whole point: board/nrf52833_lock.ld is the one file that owns the flash
 * map, and a staging writer that disagreed with it about where the partition
 * ends would be discovered by overwriting the key-value store at 0x7e000 -- the
 * exact failure the map was pinned to prevent.
 *
 * PRIMARY is deliberately the slot MCUboot validates, header included, and not
 * the payload region this image was linked against. The applier patches what
 * the bootloader will verify; a patch computed against the payload alone would
 * apply cleanly and then fail its signature check on the next boot.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <woz_flash.h>

#include "woz_freertos_platform.h"

#include <nrfx.h>

#define TAG "flash_area"

/* Placed by board/nrf52833_lock.ld. Their addresses are the values; the
 * objects themselves have no storage, which is why they are taken by address
 * and never read. */
extern uint32_t __woz_patch_staging_start;
extern uint32_t __woz_patch_staging_end;

/*
 * The primary slot, which starts one pad before this image.
 *
 * mcuboot_pad is 0x200 and holds the signed image header, so the slot MCUboot
 * verifies begins at 0xa000 while the linker placed us at 0xa200. Written as
 * the arithmetic rather than as 0xa000 so the relationship survives someone
 * moving the map: the pad size is the thing that is fixed by MCUboot, and the
 * origin is the thing that moves with it.
 */
#define WOZ_MCUBOOT_PAD_SIZE 0x200u

extern uint32_t __woz_vector_table;

struct woz_flash_area {
	uint32_t offset; /* absolute address of the first byte */
	uint32_t size;
};

static struct woz_flash_area s_primary;
static struct woz_flash_area s_staging;

int woz_flash_open(enum woz_flash_area_id id, const struct woz_flash_area **fa)
{
	const uint32_t staging_start = (uint32_t)&__woz_patch_staging_start;
	const uint32_t staging_end = (uint32_t)&__woz_patch_staging_end;
	const uint32_t image_start = (uint32_t)&__woz_vector_table;

	if (fa == NULL) {
		return -1;
	}

	switch (id) {
	case WOZ_FLASH_AREA_PRIMARY:
		s_primary.offset = image_start - WOZ_MCUBOOT_PAD_SIZE;
		/* The slot runs from the pad up to the staging partition, which
		 * is what mcuboot_primary spans in the pinned map. */
		s_primary.size = staging_start - s_primary.offset;
		*fa = &s_primary;
		return 0;
	case WOZ_FLASH_AREA_STAGING:
		s_staging.offset = staging_start;
		s_staging.size = staging_end - staging_start;
		*fa = &s_staging;
		return 0;
	default:
		return -1;
	}
}

void woz_flash_close(const struct woz_flash_area *fa)
{
	/* Nothing is held open: the areas are static and the driver below has
	 * no per-open state. Present because the contract has it, and because a
	 * backend that needed it later should not change every caller. */
	(void)fa;
}

size_t woz_flash_size(const struct woz_flash_area *fa)
{
	return fa != NULL ? (size_t)fa->size : 0u;
}

/*
 * Bounds, computed so they cannot wrap.
 *
 * off + len is the obvious test and it is wrong: both are uint32_t, and a
 * caller passing a length near UINT32_MAX would wrap the sum back inside the
 * partition and pass. Subtracting from the size instead can only underflow if
 * off already exceeds it, which is tested first.
 */
static bool in_bounds(const struct woz_flash_area *fa, uint32_t off, size_t len)
{
	if (fa == NULL) {
		return false;
	}
	if (off > fa->size) {
		return false;
	}
	return len <= (size_t)(fa->size - off);
}

int woz_flash_read(const struct woz_flash_area *fa, uint32_t off, void *dst, size_t len)
{
	if (dst == NULL || !in_bounds(fa, off, len)) {
		return -1;
	}
	if (len == 0u) {
		return 0;
	}
	/* Flash is memory-mapped for reads on this part, and the DFU applier
	 * reads far more than it writes. Going through the driver would buy
	 * nothing and cost a timeslot request per read. */
	memcpy(dst, (const void *)(uintptr_t)(fa->offset + off), len);
	return 0;
}

int woz_flash_write(const struct woz_flash_area *fa, uint32_t off, const void *src, size_t len)
{
	if (src == NULL || !in_bounds(fa, off, len)) {
		return -1;
	}
	if (len == 0u) {
		return 0;
	}
	if ((off % WOZ_FREERTOS_FLASH_WRITE_ALIGN) != 0u ||
	    (len % WOZ_FREERTOS_FLASH_WRITE_ALIGN) != 0u) {
		return -1;
	}
	return woz_freertos_flash_write(fa->offset + off, src, len);
}

int woz_flash_erase(const struct woz_flash_area *fa, uint32_t off, size_t len)
{
	if (!in_bounds(fa, off, len)) {
		return -1;
	}
	if (len == 0u) {
		return 0;
	}
	if ((off % WOZ_FREERTOS_FLASH_PAGE_SIZE) != 0u ||
	    (len % WOZ_FREERTOS_FLASH_PAGE_SIZE) != 0u) {
		return -1;
	}
	return woz_freertos_flash_erase(fa->offset + off, len);
}

void woz_reboot(void)
{
	woz_freertos_log(WOZ_FREERTOS_LOG_WARNING, TAG, "rebooting to apply a staged update");

	/*
	 * That line may not be read, and there is no flush to call: RTT is
	 * host-polled, so a message written microseconds before a reset can be
	 * overwritten or simply missed depending on when the debugger last
	 * looked. It is left in because it costs nothing and is usually seen,
	 * not because it is a guarantee. The record that survives a reboot is
	 * the staging partition's control page, which the receiver has already
	 * written by the time this runs.
	 */
	NVIC_SystemReset();
	for (;;) {
		/* NVIC_SystemReset does not return. */
	}
}
