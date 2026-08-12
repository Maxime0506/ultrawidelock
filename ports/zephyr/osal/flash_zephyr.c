/*
 * flash_zephyr.c - the Zephyr backend of ultrawidelock_flash.h: flash_map areas named
 * by the partition-manager ids from pm_config.h, so the mapping is decided
 * by the image that compiles this file (app and MCUboot both build ultrawidelock_dfu,
 * each against its own pm_config.h). Alignment is the driver's to enforce
 * here; the host backend enforces the same rules deliberately.
 */
#if defined(__ZEPHYR__)

#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include <pm_config.h>

#include "ultrawidelock_flash.h"

static uint8_t pm_id_of(enum ultrawidelock_flash_area_id id)
{
	return id == ULTRAWIDELOCK_FLASH_AREA_PRIMARY ? PM_MCUBOOT_PRIMARY_ID : PM_PATCH_STAGING_ID;
}

int ultrawidelock_flash_open(enum ultrawidelock_flash_area_id id,
			     const struct ultrawidelock_flash_area **fa)
{
	return flash_area_open(pm_id_of(id), (const struct flash_area **)fa);
}

void ultrawidelock_flash_close(const struct ultrawidelock_flash_area *fa)
{
	flash_area_close((const struct flash_area *)fa);
}

size_t ultrawidelock_flash_size(const struct ultrawidelock_flash_area *fa)
{
	return ((const struct flash_area *)fa)->fa_size;
}

int ultrawidelock_flash_read(const struct ultrawidelock_flash_area *fa, uint32_t off, void *dst,
			     size_t len)
{
	return flash_area_read((const struct flash_area *)fa, off, dst, len);
}

int ultrawidelock_flash_write(const struct ultrawidelock_flash_area *fa, uint32_t off,
			      const void *src, size_t len)
{
	return flash_area_write((const struct flash_area *)fa, off, src, len);
}

int ultrawidelock_flash_erase(const struct ultrawidelock_flash_area *fa, uint32_t off, size_t len)
{
	return flash_area_erase((const struct flash_area *)fa, off, len);
}

void ultrawidelock_reboot(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}

#endif /* __ZEPHYR__ */
