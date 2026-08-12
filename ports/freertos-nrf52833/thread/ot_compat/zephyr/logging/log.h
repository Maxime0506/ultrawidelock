/*
 * Zephyr's logging macros over the port's log sink. The module name is kept as
 * the tag so the radio platform's lines stay distinguishable, and the level
 * gate is Zephyr's own: a line above CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL
 * compiles away, arguments and all.
 */
#ifndef WOZ_OT_COMPAT_ZEPHYR_LOG_H
#define WOZ_OT_COMPAT_ZEPHYR_LOG_H

#include <woz_freertos_platform.h>

/* Zephyr's levels: 0 none, 1 error, 2 warning, 3 info, 4 debug. */
#define LOG_MODULE_REGISTER(name, level) static const char woz_ot_log_tag[] = #name

#define WOZ_OT_LOG(want, sink, ...)                                                                \
	do {                                                                                       \
		if ((want) <= CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL) {                              \
			woz_freertos_log((sink), woz_ot_log_tag, __VA_ARGS__);                     \
		}                                                                                  \
	} while (0)

#define LOG_ERR(...) WOZ_OT_LOG(1, WOZ_FREERTOS_LOG_ERROR, __VA_ARGS__)
#define LOG_WRN(...) WOZ_OT_LOG(2, WOZ_FREERTOS_LOG_WARNING, __VA_ARGS__)
#define LOG_INF(...) WOZ_OT_LOG(3, WOZ_FREERTOS_LOG_INFO, __VA_ARGS__)
#define LOG_DBG(...) WOZ_OT_LOG(4, WOZ_FREERTOS_LOG_DEBUG, __VA_ARGS__)

#endif /* WOZ_OT_COMPAT_ZEPHYR_LOG_H */
