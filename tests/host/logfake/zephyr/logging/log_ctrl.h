/* logfake: minimal <zephyr/logging/log_ctrl.h>. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_CTRL_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_CTRL_H

#include <stdint.h>

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_msg.h>

typedef log_timestamp_t (*log_timestamp_get_t)(void);

int log_set_timestamp_func(log_timestamp_get_t fn, uint32_t freq);

/* NULL backend + LOG_LEVEL_NONE is the whole-source mute woz_logquiet uses. */
uint32_t log_filter_set(struct log_backend *backend, uint32_t domain_id,
			int16_t source_id, uint32_t level);

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_CTRL_H */
