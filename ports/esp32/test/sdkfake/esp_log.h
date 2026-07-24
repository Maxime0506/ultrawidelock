/* sdkfake esp_log.h — logging macros compile (format-checked) but print nothing. */
#ifndef SDKFAKE_ESP_LOG_H
#define SDKFAKE_ESP_LOG_H

#include "sdkfake.h"
#include "esp_attr.h" /* ESP-IDF's esp_log.h drags the attrs in; sources rely on it */

typedef enum {
	ESP_LOG_NONE,
	ESP_LOG_ERROR,
	ESP_LOG_WARN,
	ESP_LOG_INFO,
	ESP_LOG_DEBUG,
	ESP_LOG_VERBOSE,
} esp_log_level_t;

void esp_log_level_set(const char *tag, esp_log_level_t level); /* recording double */

/* `if (0)` keeps the arguments referenced + format-checked without output. */
#define ESP_LOG_NOP(tag, fmt, ...)                                             \
	do {                                                                   \
		(void)(tag);                                                   \
		if (0) {                                                       \
			printf(fmt, ##__VA_ARGS__);                            \
		}                                                              \
	} while (0)

#define ESP_LOGE(tag, fmt, ...) ESP_LOG_NOP(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_NOP(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_NOP(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ESP_LOG_NOP(tag, fmt, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEX(tag, buf, len)                                      \
	do {                                                                   \
		(void)(tag);                                                   \
		(void)(buf);                                                   \
		(void)(len);                                                   \
	} while (0)

#endif
