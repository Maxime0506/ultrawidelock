/* nfcfake: <zephyr/logging/log.h>. The transports log heavily and none of it
 * is under test, so every macro is a no-op that still type-checks its
 * arguments away. */
#ifndef NFCFAKE_ZEPHYR_LOGGING_LOG_H
#define NFCFAKE_ZEPHYR_LOGGING_LOG_H

#define LOG_MODULE_REGISTER(...)
#define LOG_MODULE_DECLARE(...)

#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)

#define LOG_HEXDUMP_ERR(...) ((void)0)
#define LOG_HEXDUMP_WRN(...) ((void)0)
#define LOG_HEXDUMP_INF(...) ((void)0)
#define LOG_HEXDUMP_DBG(...) ((void)0)

#ifndef BUILD_ASSERT
#define BUILD_ASSERT(cond, ...) _Static_assert((cond), "" __VA_ARGS__)
#endif

#endif /* NFCFAKE_ZEPHYR_LOGGING_LOG_H */
