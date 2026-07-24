/* logfake: minimal <zephyr/logging/log_output.h>. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log_msg.h>

#define LOG_OUTPUT_FLAG_COLORS 2u
#define LOG_OUTPUT_CUSTOM      3u

typedef int (*log_output_func_t)(uint8_t *buf, size_t size, void *ctx);

struct log_output_control_block {
	void *ctx;
};

struct log_output {
	log_output_func_t func;
	struct log_output_control_block *control_block;
};

/* Stock-renderer delegation; the fake records the call. */
void log_output_msg_process(const struct log_output *output, struct log_msg *msg,
			    uint32_t flags);

/* Deliver len bytes through func — the fake forwards them so the suite's
 * capture sink sees exactly what the formatter emitted. */
void log_output_write(log_output_func_t func, uint8_t *buf, size_t len, void *ctx);

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_OUTPUT_H */
