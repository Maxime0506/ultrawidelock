/*
 * Hooks supplied by the selected nRF52833 BSP.
 *
 * FreeRTOS provides scheduling but not a portable high-resolution clock,
 * cycle counter, busy wait, or logger. Keeping those operations here prevents
 * BSP headers from leaking into the shared protocol modules.
 */
#ifndef WOZ_FREERTOS_PLATFORM_H
#define WOZ_FREERTOS_PLATFORM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

enum woz_freertos_log_level {
	WOZ_FREERTOS_LOG_RAW,
	WOZ_FREERTOS_LOG_ERROR,
	WOZ_FREERTOS_LOG_WARNING,
	WOZ_FREERTOS_LOG_INFO,
	WOZ_FREERTOS_LOG_DEBUG,
};

/** Monotonic microseconds since boot. This value must not step backwards. */
int64_t woz_freertos_uptime_us(void);

/** Busy wait without yielding. The implementation must accept the full range. */
void woz_freertos_busy_wait_us(uint64_t us);

/** Free-running counter used to measure the DW3110 response-arm path. */
uint32_t woz_freertos_cycle_get_32(void);

/** Fill a buffer from the nRF52833 hardware entropy source. Returns zero on success. */
int woz_freertos_entropy(void *buffer, size_t length);

/** Current die temperature in whole degrees Celsius. */
int8_t woz_freertos_die_temperature_c(void);

/** Stop or reset after an unrecoverable platform failure. This function must not return. */
_Noreturn void woz_freertos_fatal(const char *reason);

/*
 * Internal flash, as the key-value store uses it. Offsets are absolute in the
 * part's address space. The BSP owns these because writing flash on this part
 * has to be coordinated with the radio: MPSL arbitrates the NVMC stall against
 * its own timeslots, and a write issued outside that arbitration can overrun a
 * radio event.
 *
 * The semantics are the part's own, and callers depend on them: an erased byte
 * reads as 0xff, a write may only clear bits, writes are word-aligned and a
 * whole multiple of four bytes long, and an erase covers whole pages.
 */
#define WOZ_FREERTOS_FLASH_PAGE_SIZE 4096u
#define WOZ_FREERTOS_FLASH_WRITE_ALIGN 4u

/** Read from flash. Returns zero on success. */
int woz_freertos_flash_read(uint32_t offset, void *buffer, size_t length);

/** Write to flash. offset and length must be word-aligned. Zero on success. */
int woz_freertos_flash_write(uint32_t offset, const void *data, size_t length);

/** Erase whole pages. offset and length must be page-aligned. Zero on success. */
int woz_freertos_flash_erase(uint32_t offset, size_t length);

/** Platform log sink. It must be safe from every task that uses shared code. */
void woz_freertos_log(enum woz_freertos_log_level level, const char *tag, const char *fmt, ...);

/**
 * The same sink taking an already-started va_list, for callers handed one.
 * OpenThread's otPlatLog is the reason this exists: its contract passes the
 * arguments through, so it cannot use the variadic form above.
 */
void woz_freertos_log_va(enum woz_freertos_log_level level, const char *tag, const char *fmt,
			 va_list args);

/** Hex log sink matching the Zephyr LOG_HEXDUMP_* contract. */
void woz_freertos_log_hexdump(enum woz_freertos_log_level level, const char *tag,
			      const void *data, size_t len, const char *message);

#endif /* WOZ_FREERTOS_PLATFORM_H */
