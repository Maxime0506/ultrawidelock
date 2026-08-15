/*
 * Hooks supplied by the selected nRF52833 BSP.
 *
 * FreeRTOS provides scheduling but not a portable high-resolution clock,
 * cycle counter, busy wait, or logger. Keeping those operations here prevents
 * BSP headers from leaking into the shared protocol modules.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_PLATFORM_H
#define ULTRAWIDELOCK_FREERTOS_PLATFORM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum ultrawidelock_freertos_log_level {
	ULTRAWIDELOCK_FREERTOS_LOG_RAW,
	ULTRAWIDELOCK_FREERTOS_LOG_ERROR,
	ULTRAWIDELOCK_FREERTOS_LOG_WARNING,
	ULTRAWIDELOCK_FREERTOS_LOG_INFO,
	ULTRAWIDELOCK_FREERTOS_LOG_DEBUG,
};

/** Monotonic microseconds since boot. This value must not step backwards. */
int64_t ultrawidelock_freertos_uptime_us(void);

/** Busy wait without yielding. The implementation must accept the full range. */
void ultrawidelock_freertos_busy_wait_us(uint64_t us);

/** Free-running counter used to measure the DW3110 response-arm path. */
uint32_t ultrawidelock_freertos_cycle_get_32(void);

/** Fill a buffer from the nRF52833 hardware entropy source. Returns zero on success. */
int ultrawidelock_freertos_entropy(void *buffer, size_t length);

/** Current die temperature in whole degrees Celsius. */
int8_t ultrawidelock_freertos_die_temperature_c(void);

/** Stop or reset after an unrecoverable platform failure. This function must not return. */
_Noreturn void ultrawidelock_freertos_fatal(const char *reason);

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
#define ULTRAWIDELOCK_FREERTOS_FLASH_PAGE_SIZE 4096u
#define ULTRAWIDELOCK_FREERTOS_FLASH_WRITE_ALIGN 4u

/** Read from flash. Returns zero on success. */
int ultrawidelock_freertos_flash_read(uint32_t offset, void *buffer, size_t length);

/** Write to flash. offset and length must be word-aligned. Zero on success. */
int ultrawidelock_freertos_flash_write(uint32_t offset, const void *data, size_t length);

/** Erase whole pages. offset and length must be page-aligned. Zero on success. */
int ultrawidelock_freertos_flash_erase(uint32_t offset, size_t length);

/**
 * May this range be written or erased at all?
 *
 * The driver confines writes to a window that excludes the application image,
 * so a store with an offset bug can lose its own data but cannot erase the
 * firmware it is running. Exposed because the window is a BUILD-TIME decision
 * and the partitions it has to cover are a LINKER decision: whoever owns a
 * partition can ask, at start-up, whether it is actually writable, rather than
 * finding out when a user tries to use it.
 */
bool ultrawidelock_freertos_flash_writable(uint32_t offset, size_t length);

/** Platform log sink. It must be safe from every task that uses shared code. */
void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag, const char *fmt, ...);

/**
 * The same sink taking an already-started va_list, for callers handed one.
 * OpenThread's otPlatLog is the reason this exists: its contract passes the
 * arguments through, so it cannot use the variadic form above.
 */
void ultrawidelock_freertos_log_va(enum ultrawidelock_freertos_log_level level, const char *tag, const char *fmt,
			 va_list args);

/** Hex log sink matching the Zephyr LOG_HEXDUMP_* contract. */
void ultrawidelock_freertos_log_hexdump(enum ultrawidelock_freertos_log_level level, const char *tag,
			      const void *data, size_t len, const char *message);

/*
 * THE LEVEL GATE HAS TO BE AT THE CALL SITE, not inside the sink.
 *
 * It used to be inside: ultrawidelock_freertos_log() compared the level and returned
 * early. That suppresses the OUTPUT and keeps every format string, every
 * argument evaluation and every call in the image, so lowering the level cost a
 * rebuild and saved nothing. Measured before this changed -- an image built at
 * WARNING was the same size as one built at INFO, to the byte.
 *
 * As a macro over the same name the comparison happens on a constant, the
 * branch folds away, and the string it referenced becomes unreachable for
 * --gc-sections to drop. Every existing call site keeps working unchanged: C
 * forbids a function-like macro from expanding inside its own expansion, so the
 * inner name is the real function.
 *
 * The two calls that pass a computed level both live in the sink's own
 * implementation, which defines ULTRAWIDELOCK_FREERTOS_LOG_NO_MACRO before including this
 * header -- it has to, or the macro would rewrite the function's own
 * definition.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL
#define ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL ULTRAWIDELOCK_FREERTOS_LOG_INFO
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_LOG_NO_MACRO
#define ultrawidelock_freertos_log(level, ...)                                                               \
	do {                                                                                       \
		if ((level) <= (ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL)) {                                     \
			ultrawidelock_freertos_log((level), __VA_ARGS__);                                    \
		}                                                                                  \
	} while (0)

#define ultrawidelock_freertos_log_hexdump(level, tag, data, len, message)                                   \
	do {                                                                                       \
		if ((level) <= (ULTRAWIDELOCK_FREERTOS_LOG_MAX_LEVEL)) {                                     \
			ultrawidelock_freertos_log_hexdump((level), (tag), (data), (len), (message));        \
		}                                                                                  \
	} while (0)
#endif

#endif /* ULTRAWIDELOCK_FREERTOS_PLATFORM_H */
