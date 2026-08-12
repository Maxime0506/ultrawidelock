/** @file ultrawidelock_uwb_msg.h — message framing accessors, dispatch and builders. */

#pragma once

#include "ultrawidelock_uwb_internal.h"

#include <ultrawidelock_uwb_adapter/ultrawidelock_uwb_session.h>

#include <stdint.h>

/* Header field accessors (operate on the raw message bytes). */
uint8_t ultrawidelock_uwb_msg_protocol_header(const uint8_t *bytes);
uint8_t ultrawidelock_uwb_msg_message_id(const uint8_t *bytes);
uint16_t ultrawidelock_uwb_msg_payload_length(const uint8_t *bytes);

/* Protocol dispatch, called from ultrawidelock_uwb_session_message_handle(). */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_msg_process_ranging(struct ultrawidelock_uwb_session *session,
				      struct ultrawidelock_uwb_message *message);
enum ultrawidelock_uwb_err
ultrawidelock_uwb_msg_process_notification(struct ultrawidelock_uwb_session *session,
					   struct ultrawidelock_uwb_message *message);
enum ultrawidelock_uwb_err
ultrawidelock_uwb_msg_process_supplementary(struct ultrawidelock_uwb_session *session,
					    struct ultrawidelock_uwb_message *message);

/* Message builders (used by the session lifecycle). */
struct ultrawidelock_uwb_message *
ultrawidelock_uwb_msg_build_m1(struct ultrawidelock_uwb_session *session);
struct ultrawidelock_uwb_message *
ultrawidelock_uwb_msg_build_suspend_resume_request(struct ultrawidelock_uwb_session *session,
						   bool suspend);
/** Build a general-error message for the given session. */
struct ultrawidelock_uwb_message *
ultrawidelock_uwb_msg_build_general_error(struct ultrawidelock_uwb_session *session,
					  uint8_t error_code);

/**
 * @brief Release a message built by this layer.
 * @param message Message to free.
 */
void ultrawidelock_uwb_msg_free(struct ultrawidelock_uwb_message *message);
