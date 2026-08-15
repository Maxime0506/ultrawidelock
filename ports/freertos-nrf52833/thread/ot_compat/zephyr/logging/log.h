/* SPDX-License-Identifier: ISC */

/*
 * Zephyr's logging macros over the port's log sink. The module name is kept as
 * the tag so the radio platform's lines stay distinguishable, and the level
 * gate is Zephyr's own: a line above CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL
 * compiles away, arguments and all.
 */
#ifndef ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_LOG_H
#define ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_LOG_H

#include <ultrawidelock_freertos_platform.h>

/* Zephyr's levels: 0 none, 1 error, 2 warning, 3 info, 4 debug. */
#define LOG_MODULE_REGISTER(name, level) static const char ultrawidelock_ot_log_tag[] = #name

#define ULTRAWIDELOCK_OT_LOG(want, sink, ...)                                                      \
	do {                                                                                       \
		if ((want) <= CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL) {                              \
			ultrawidelock_freertos_log((sink), ultrawidelock_ot_log_tag, __VA_ARGS__); \
		}                                                                                  \
	} while (0)

#define LOG_ERR(...) ULTRAWIDELOCK_OT_LOG(1, ULTRAWIDELOCK_FREERTOS_LOG_ERROR, __VA_ARGS__)
#define LOG_WRN(...) ULTRAWIDELOCK_OT_LOG(2, ULTRAWIDELOCK_FREERTOS_LOG_WARNING, __VA_ARGS__)
#define LOG_INF(...) ULTRAWIDELOCK_OT_LOG(3, ULTRAWIDELOCK_FREERTOS_LOG_INFO, __VA_ARGS__)
#define LOG_DBG(...) ULTRAWIDELOCK_OT_LOG(4, ULTRAWIDELOCK_FREERTOS_LOG_DEBUG, __VA_ARGS__)

#endif /* ULTRAWIDELOCK_OT_COMPAT_ZEPHYR_LOG_H */
