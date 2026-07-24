/* logfake: minimal <zephyr/logging/log_msg.h>. The suite fabricates log_msg
 * structs directly; the accessors are the ones woz_logfmt.c calls. */
#ifndef LOGFAKE_ZEPHYR_LOGGING_LOG_MSG_H
#define LOGFAKE_ZEPHYR_LOGGING_LOG_MSG_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t log_timestamp_t;

struct log_msg {
	uint8_t level;
	uint8_t domain;
	int16_t source_id;
	log_timestamp_t timestamp;
	uint8_t *package; /* rendered string (see fake cbpprintf) */
	size_t plen;
	uint8_t *data; /* hexdump payload */
	size_t dlen;
};

static inline uint8_t *log_msg_get_data(struct log_msg *msg, size_t *len)
{
	*len = msg->dlen;
	return msg->data;
}
static inline uint8_t *log_msg_get_package(struct log_msg *msg, size_t *len)
{
	*len = msg->plen;
	return msg->package;
}
static inline uint8_t log_msg_get_level(struct log_msg *msg)
{
	return msg->level;
}
static inline int16_t log_msg_get_source_id(struct log_msg *msg)
{
	return msg->source_id;
}
static inline uint8_t log_msg_get_domain(struct log_msg *msg)
{
	return msg->domain;
}
static inline log_timestamp_t log_msg_get_timestamp(struct log_msg *msg)
{
	return msg->timestamp;
}

#endif /* LOGFAKE_ZEPHYR_LOGGING_LOG_MSG_H */
