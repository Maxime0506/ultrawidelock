/*
 * flash_zephyr.c - the Zephyr backend of woz_flash.h: flash_map areas named
 * by the partition-manager ids from pm_config.h, so the mapping is decided
 * by the image that compiles this file (app and MCUboot both build ultrawidelock_dfu,
 * each against its own pm_config.h). Alignment is the driver's to enforce
 * here; the host backend enforces the same rules deliberately.
 */
#if defined(__ZEPHYR__)

#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include <pm_config.h>

#include "woz_flash.h"

static uint8_t pm_id_of(enum woz_flash_area_id id)
{
	return id == WOZ_FLASH_AREA_PRIMARY ? PM_MCUBOOT_PRIMARY_ID : PM_PATCH_STAGING_ID;
}

int woz_flash_open(enum woz_flash_area_id id, const struct woz_flash_area **fa)
{
	return flash_area_open(pm_id_of(id), (const struct flash_area **)fa);
}

void woz_flash_close(const struct woz_flash_area *fa)
{
	flash_area_close((const struct flash_area *)fa);
}

size_t woz_flash_size(const struct woz_flash_area *fa)
{
	return ((const struct flash_area *)fa)->fa_size;
}

int woz_flash_read(const struct woz_flash_area *fa, uint32_t off, void *dst, size_t len)
{
	return flash_area_read((const struct flash_area *)fa, off, dst, len);
}

int woz_flash_write(const struct woz_flash_area *fa, uint32_t off, const void *src, size_t len)
{
	return flash_area_write((const struct flash_area *)fa, off, src, len);
}

int woz_flash_erase(const struct woz_flash_area *fa, uint32_t off, size_t len)
{
	return flash_area_erase((const struct flash_area *)fa, off, len);
}

void woz_reboot(void)
{
	sys_reboot(SYS_REBOOT_COLD);
}

#endif /* __ZEPHYR__ */
