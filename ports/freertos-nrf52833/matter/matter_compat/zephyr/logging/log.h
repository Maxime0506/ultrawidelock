/*
 * Zephyr's logging macros over the port's log sink, for the shared Matter
 * transport. Same shape as thread/ot_compat's, with this file's own level
 * symbol so the two can be turned down independently.
 *
 * The level gate is Zephyr's: a line above CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL
 * compiles away, arguments and all.
 */
#ifndef WOZ_MATTER_COMPAT_ZEPHYR_LOG_H
#define WOZ_MATTER_COMPAT_ZEPHYR_LOG_H

#include <woz_freertos_platform.h>

#define LOG_MODULE_REGISTER(name, level) static const char woz_matter_log_tag[] = #name
#define LOG_MODULE_DECLARE(name, level)  extern const char woz_matter_log_tag[]

#define WOZ_MATTER_LOG(want, sink, ...)                                                            \
	do {                                                                                       \
		if ((want) <= CONFIG_ALIRO_MATTER_BLE_LOG_LEVEL) {                                 \
			woz_freertos_log((sink), woz_matter_log_tag, __VA_ARGS__);                 \
		}                                                                                  \
	} while (0)

#define LOG_ERR(...) WOZ_MATTER_LOG(1, WOZ_FREERTOS_LOG_ERROR, __VA_ARGS__)
#define LOG_WRN(...) WOZ_MATTER_LOG(2, WOZ_FREERTOS_LOG_WARNING, __VA_ARGS__)
#define LOG_INF(...) WOZ_MATTER_LOG(3, WOZ_FREERTOS_LOG_INFO, __VA_ARGS__)
#define LOG_DBG(...) WOZ_MATTER_LOG(4, WOZ_FREERTOS_LOG_DEBUG, __VA_ARGS__)

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_LOG_H */
