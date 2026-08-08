/*
 * flash_host.c - the host backend of woz_flash.h: RAM partitions that enforce
 * the nRF alignment rules (word writes, page erases). The strictness is the
 * point -- the DFU applier's write combiner and erase rounding exist to meet
 * these rules, and a fake accepting anything would hide their bugs. Geometry
 * and failure-knob semantics carried over from tests/host/dfufake, the prior
 * home of this model.
 */
#if defined(WOZ_PORT_HOST)

#include <string.h>

#include "woz_flash.h"

static uint8_t g_primary_buf[WOZ_FLASH_HOST_PRIMARY_SIZE];
static uint8_t g_staging_buf[WOZ_FLASH_HOST_STAGING_SIZE];

static struct woz_flash_host_area g_areas[2] = {
	[WOZ_FLASH_AREA_PRIMARY] = { .buf = g_primary_buf, .size = sizeof(g_primary_buf) },
	[WOZ_FLASH_AREA_STAGING] = { .buf = g_staging_buf, .size = sizeof(g_staging_buf) },
};

static unsigned g_reboots;

struct woz_flash_host_area *woz_flash_host_area(enum woz_flash_area_id id)
{
	return &g_areas[id];
}

unsigned woz_flash_host_reboots(void)
{
	return g_reboots;
}

void woz_flash_host_reset(void)
{
	for (int i = 0; i < 2; i++) {
		uint8_t *buf = g_areas[i].buf;
		size_t size = g_areas[i].size;

		memset(&g_areas[i], 0, sizeof(g_areas[i]));
		g_areas[i].buf = buf;
		g_areas[i].size = size;
		g_areas[i].write_fail_in = -1;
		g_areas[i].erase_fail_in = -1;
		g_areas[i].read_fail_in = -1;
		memset(buf, 0xff, size);
	}
	g_reboots = 0;
}

/* -1 never fires, 0 fires now and forever, N > 0 lets N calls through. */
static int knob_fires(int *knob)
{
	if (*knob < 0) {
		return 0;
	}
	if (*knob == 0) {
		return 1;
	}
	(*knob)--;
	return 0;
}

/* The opaque handle is the host-area struct itself. */
static struct woz_flash_host_area *area_of(const struct woz_flash_area *fa)
{
	return (struct woz_flash_host_area *)(void *)(uintptr_t)fa;
}

int woz_flash_open(enum woz_flash_area_id id, const struct woz_flash_area **fa)
{
	struct woz_flash_host_area *a = &g_areas[id];

	a->open_calls++;
	if (a->fail_open) {
		return -1;
	}
	*fa = (const struct woz_flash_area *)a;
	return 0;
}

void woz_flash_close(const struct woz_flash_area *fa)
{
	area_of(fa)->close_calls++;
}

size_t woz_flash_size(const struct woz_flash_area *fa)
{
	return area_of(fa)->size;
}

int woz_flash_read(const struct woz_flash_area *fa, uint32_t off, void *dst, size_t len)
{
	struct woz_flash_host_area *a = area_of(fa);

	a->read_calls++;
	if (knob_fires(&a->read_fail_in)) {
		return -1;
	}
	if (off > a->size || len > a->size - off) {
		return -1;
	}
	memcpy(dst, a->buf + off, len);
	return 0;
}

int woz_flash_write(const struct woz_flash_area *fa, uint32_t off, const void *src, size_t len)
{
	struct woz_flash_host_area *a = area_of(fa);

	a->write_calls++;
	a->last_write_off = off;
	a->last_write_len = len;
	if (knob_fires(&a->write_fail_in)) {
		return -1;
	}
	if (off % WOZ_FLASH_HOST_WRITE_BLOCK || len % WOZ_FLASH_HOST_WRITE_BLOCK) {
		return -1; /* the nRF driver refuses unaligned writes */
	}
	if (off > a->size || len > a->size - off) {
		return -1;
	}
	memcpy(a->buf + off, src, len);
	return 0;
}

int woz_flash_erase(const struct woz_flash_area *fa, uint32_t off, size_t len)
{
	struct woz_flash_host_area *a = area_of(fa);

	a->erase_calls++;
	a->last_erase_off = off;
	a->last_erase_len = len;
	if (knob_fires(&a->erase_fail_in)) {
		return -1;
	}
	if (off % WOZ_FLASH_HOST_PAGE_SIZE || len % WOZ_FLASH_HOST_PAGE_SIZE) {
		return -1; /* the nRF driver erases whole pages only */
	}
	if (off > a->size || len > a->size - off) {
		return -1;
	}
	memset(a->buf + off, 0xff, len);
	return 0;
}

void woz_reboot(void)
{
	g_reboots++;
}

#endif /* WOZ_PORT_HOST */
