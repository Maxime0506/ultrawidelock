/*
 * Zephyr's logging macros over the port's log sink, for the shared Matter
 * transport. Same shape as thread/ot_compat's, with this file's own level
 * symbol so the two can be turned down independently.
 *
 * The level gate is Zephyr's: a line above CONFIG_ULTRAWIDELOCK_MATTER_BLE_LOG_LEVEL
 * compiles away, arguments and all.
 */
#ifndef ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_LOG_H
#define ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_LOG_H

#include <ultrawidelock_freertos_platform.h>

/*
 * Both spellings define a file-scope tag rather than sharing one. Zephyr's
 * DECLARE refers to a tag another translation unit REGISTERED, which would need
 * this one to be extern -- and then two shared files that both register the same
 * module name would collide at link time. A private copy per file costs a few
 * bytes of rodata and cannot.
 */
#define LOG_MODULE_REGISTER(name, level) static const char ultrawidelock_matter_log_tag[] = #name
#define LOG_MODULE_DECLARE(name, level)  static const char ultrawidelock_matter_log_tag[] = #name

#define ULTRAWIDELOCK_MATTER_LOG(want, sink, ...)                                                            \
	do {                                                                                       \
		if ((want) <= CONFIG_ULTRAWIDELOCK_MATTER_BLE_LOG_LEVEL) {                                 \
			ultrawidelock_freertos_log((sink), ultrawidelock_matter_log_tag, __VA_ARGS__);                 \
		}                                                                                  \
	} while (0)

#define LOG_ERR(...) ULTRAWIDELOCK_MATTER_LOG(1, ULTRAWIDELOCK_FREERTOS_LOG_ERROR, __VA_ARGS__)
#define LOG_WRN(...) ULTRAWIDELOCK_MATTER_LOG(2, ULTRAWIDELOCK_FREERTOS_LOG_WARNING, __VA_ARGS__)
#define LOG_INF(...) ULTRAWIDELOCK_MATTER_LOG(3, ULTRAWIDELOCK_FREERTOS_LOG_INFO, __VA_ARGS__)
#define LOG_DBG(...) ULTRAWIDELOCK_MATTER_LOG(4, ULTRAWIDELOCK_FREERTOS_LOG_DEBUG, __VA_ARGS__)

/*
 * Hex dumps go to the port's own sink, which already implements the Zephyr
 * LOG_HEXDUMP_* contract -- same arguments, same meaning. An earlier version of
 * this header formatted the bytes itself, which was a second implementation of
 * something the platform already had.
 *
 * Not merely a debug aid: LOG_HEXDUMP_ERR is how a malformed SPAKE2+ verifier
 * is reported, and a verifier that does not parse is the difference between a
 * node that refuses every commissioning attempt and one nobody can explain.
 */
#define ULTRAWIDELOCK_MATTER_HEXDUMP(want, sink, data, len, label)                                           \
	do {                                                                                       \
		if ((want) <= CONFIG_ULTRAWIDELOCK_MATTER_BLE_LOG_LEVEL) {                                 \
			ultrawidelock_freertos_log_hexdump((sink), ultrawidelock_matter_log_tag, (data), (len),        \
						 (label));                                         \
		}                                                                                  \
	} while (0)

#define LOG_HEXDUMP_ERR(d, l, s) ULTRAWIDELOCK_MATTER_HEXDUMP(1, ULTRAWIDELOCK_FREERTOS_LOG_ERROR, d, l, s)
#define LOG_HEXDUMP_WRN(d, l, s) ULTRAWIDELOCK_MATTER_HEXDUMP(2, ULTRAWIDELOCK_FREERTOS_LOG_WARNING, d, l, s)
#define LOG_HEXDUMP_INF(d, l, s) ULTRAWIDELOCK_MATTER_HEXDUMP(3, ULTRAWIDELOCK_FREERTOS_LOG_INFO, d, l, s)
#define LOG_HEXDUMP_DBG(d, l, s) ULTRAWIDELOCK_MATTER_HEXDUMP(4, ULTRAWIDELOCK_FREERTOS_LOG_DEBUG, d, l, s)

#endif /* ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_LOG_H */
