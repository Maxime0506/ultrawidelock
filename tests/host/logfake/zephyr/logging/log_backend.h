/* logfake: minimal <zephyr/logging/log_backend.h>. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_BACKEND_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_BACKEND_H

#include <stdint.h>

struct log_backend {
	int id;
};

int log_backend_count_get(void);
const struct log_backend *log_backend_get(int idx);
int log_backend_format_set(const struct log_backend *backend, uint32_t format);

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_BACKEND_H */
