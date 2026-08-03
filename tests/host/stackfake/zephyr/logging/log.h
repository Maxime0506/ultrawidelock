/* stackfake: <zephyr/logging/log.h>. The stack logs heavily and none of it is
 * under test; every macro is a no-op. */
#ifndef STACKFAKE_ZEPHYR_LOGGING_LOG_H
#define STACKFAKE_ZEPHYR_LOGGING_LOG_H

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

#endif /* STACKFAKE_ZEPHYR_LOGGING_LOG_H */
