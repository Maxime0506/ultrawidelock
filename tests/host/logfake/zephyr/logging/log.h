/* logfake: minimal <zephyr/logging/log.h>. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_H

#include <stdint.h>

#include <zephyr/logging/log_msg.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define LOG_LEVEL_NONE 0u
#define LOG_LEVEL_ERR  1u
#define LOG_LEVEL_WRN  2u
#define LOG_LEVEL_INF  3u
#define LOG_LEVEL_DBG  4u

const char *log_source_name_get(uint32_t domain_id, int16_t source_id);
int16_t log_source_id_get(const char *name);

/* No-op module/log macros for driver sources that include this header directly
 * (uwb_rxdiag.c, uwb_selftest.c). Guarded: woz_log.h may define them too. */
#ifndef LOG_MODULE_REGISTER
#define LOG_MODULE_REGISTER(...)
#endif
#ifndef LOG_MODULE_DECLARE
#define LOG_MODULE_DECLARE(...)
#endif
#ifndef LOG_ERR
#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#endif

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_H */
