/*
 * Hooks supplied by the selected nRF52833 BSP.
 *
 * FreeRTOS provides scheduling but not a portable high-resolution clock,
 * cycle counter, busy wait, or logger. Keeping those operations here prevents
 * BSP headers from leaking into the shared protocol modules.
 */
#ifndef WOZ_FREERTOS_PLATFORM_H
#define WOZ_FREERTOS_PLATFORM_H

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

/** Platform log sink. It must be safe from every task that uses shared code. */
void woz_freertos_log(enum woz_freertos_log_level level, const char *tag, const char *fmt, ...);

/** Hex log sink matching the Zephyr LOG_HEXDUMP_* contract. */
void woz_freertos_log_hexdump(enum woz_freertos_log_level level, const char *tag,
			      const void *data, size_t len, const char *message);

#endif /* WOZ_FREERTOS_PLATFORM_H */
