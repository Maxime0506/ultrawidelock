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

#define LOG_MODULE_REGISTER(name, level) static const char ultrawidelock_matter_log_tag[] = #name
#define LOG_MODULE_DECLARE(name, level)  extern const char ultrawidelock_matter_log_tag[]

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

#endif /* ULTRAWIDELOCK_MATTER_COMPAT_ZEPHYR_LOG_H */
