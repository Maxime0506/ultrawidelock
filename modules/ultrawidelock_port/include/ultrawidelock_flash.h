/*
 * ultrawidelock_flash.h - the two flash partitions ultrawidelock_dfu touches, and reboot.
 *
 * Exactly what the DFU receiver and applier need: open one of two named
 * areas, read/write/erase inside it, reboot when a patch is staged. The
 * hardware alignment rules travel with the contract -- writes are
 * write-block aligned, erases are page aligned -- and the host backend
 * ENFORCES them (flash_host.c), because the applier's write combiner and
 * erase rounding exist only to satisfy them; a permissive fake would hide
 * their bugs. Backends: Zephyr flash_map over the partition-manager ids
 * (flash_zephyr.c), host RAM (flash_host.c). ESP-IDF gets a backend when an
 * ESP consumer of ultrawidelock_dfu exists; none does today.
 */
#ifndef ULTRAWIDELOCK_FLASH_H
#define ULTRAWIDELOCK_FLASH_H

#include <stddef.h>
#include <stdint.h>

enum ultrawidelock_flash_area_id {
	ULTRAWIDELOCK_FLASH_AREA_PRIMARY,  /* the running image's slot */
	ULTRAWIDELOCK_FLASH_AREA_STAGING,  /* where a delta patch lands */
};

struct ultrawidelock_flash_area; /* opaque; each backend owns the layout */

/** @brief Open @p id. @return 0 and *@p fa set, negative on failure. */
int ultrawidelock_flash_open(enum ultrawidelock_flash_area_id id,
			     const struct ultrawidelock_flash_area **fa);
void ultrawidelock_flash_close(const struct ultrawidelock_flash_area *fa);

/** @brief Size of the area in bytes. */
size_t ultrawidelock_flash_size(const struct ultrawidelock_flash_area *fa);

/** @return 0, negative on out-of-bounds or a backend failure. */
int ultrawidelock_flash_read(const struct ultrawidelock_flash_area *fa, uint32_t off, void *dst,
			     size_t len);
/** @brief Write; @p off and @p len must be write-block aligned. */
int ultrawidelock_flash_write(const struct ultrawidelock_flash_area *fa, uint32_t off,
			      const void *src, size_t len);
/** @brief Erase to 0xff; @p off and @p len must be page aligned. */
int ultrawidelock_flash_erase(const struct ultrawidelock_flash_area *fa, uint32_t off, size_t len);

/** @brief Cold reboot. On the host backend: recorded, never taken. */
void ultrawidelock_reboot(void);

#if defined(ULTRAWIDELOCK_PORT_HOST)

/* ---- suite controls (host backend only) ---------------------------------
 *
 * Geometry matches apps/dwm3001cdk-lock/pm_static.yml so size-limit checks in the code
 * under test are the ones it will meet on the board.
 */
#define ULTRAWIDELOCK_FLASH_HOST_WRITE_BLOCK  4u
#define ULTRAWIDELOCK_FLASH_HOST_PAGE_SIZE    4096u
#define ULTRAWIDELOCK_FLASH_HOST_STAGING_SIZE 0xa000u  /* 40,960 B */
#define ULTRAWIDELOCK_FLASH_HOST_PRIMARY_SIZE 0x6a000u /* 434,176 B */

/**
 * One RAM-backed area plus the failure knobs a suite injects. Every
 * `*_fail_in` counts calls down: -1 never fails, 0 fails this call and every
 * call after it, N > 0 lets N calls through and then fails.
 */
struct ultrawidelock_flash_host_area {
	uint8_t *buf;
	size_t size;

	/* knobs */
	int fail_open;
	int write_fail_in;
	int erase_fail_in;
	int read_fail_in;

	/* recorded */
	unsigned open_calls, close_calls, write_calls, erase_calls, read_calls;
	uint32_t last_write_off;
	size_t last_write_len;
	uint32_t last_erase_off;
	size_t last_erase_len;
};

/** @brief The area behind @p id, for knobs and byte-level assertions. */
struct ultrawidelock_flash_host_area *
ultrawidelock_flash_host_area(enum ultrawidelock_flash_area_id id);

/** @brief ultrawidelock_reboot() calls recorded since reset. */
unsigned ultrawidelock_flash_host_reboots(void);

/** @brief Zero every recording, restore every knob, erase both areas. */
void ultrawidelock_flash_host_reset(void);

#endif /* ULTRAWIDELOCK_PORT_HOST */

#endif /* ULTRAWIDELOCK_FLASH_H */
