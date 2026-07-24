/* ecpfake: no-op <zephyr/logging/log.h> for the C++ ECP suite; LOG_WRN is
 * tallied so the unprovisioned-identifier warning branch is observable. */
#ifndef ECPFAKE_ZEPHYR_LOGGING_LOG_H
#define ECPFAKE_ZEPHYR_LOGGING_LOG_H

#include "rfal_rf.h" /* ecpfake state (warn tally) */

#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...)             ((void)0)
#define LOG_WRN(...)             ((void)(ecpfake.warns++))
#define LOG_INF(...)             ((void)0)
#define LOG_DBG(...)             ((void)0)
#define LOG_HEXDUMP_INF(...)     ((void)0)
#define LOG_HEXDUMP_WRN(...)     ((void)0)

#endif /* ECPFAKE_ZEPHYR_LOGGING_LOG_H */
