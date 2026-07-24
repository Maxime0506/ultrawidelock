/* logfake: minimal <zephyr/logging/log_output_custom.h>. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_CUSTOM_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_CUSTOM_H

#include <stdint.h>

#include <zephyr/logging/log_msg.h>
#include <zephyr/logging/log_output.h>

void log_custom_output_msg_set(void (*fn)(const struct log_output *output,
					  struct log_msg *msg, uint32_t flags));

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_CUSTOM_H */
