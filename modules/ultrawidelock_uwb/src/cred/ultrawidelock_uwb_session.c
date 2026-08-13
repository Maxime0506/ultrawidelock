/** @file ultrawidelock_uwb_session.c — per-session lifecycle and state machine. */

#include "ultrawidelock_uwb_internal.h"
#include "ultrawidelock_uwb_msg.h"
#include "ultrawidelock_uwb_msg_spec.h"

#include <ultrawidelock_uwb_adapter/ultrawidelock_uwb_session.h>
#include <cherry/cherry_ccc.h>

#include "ultrawidelock_alloc.h"
#include <string.h>

#include "ultrawidelock_log.h"

LOG_MODULE_DECLARE(ultrawidelock_cred_uwb, LOG_LEVEL_INF);

#define ULTRAWIDELOCK_UWB_URSK_SIZE 32

/**
 * @brief Send a general-error notification to the peer.
 * @param session Session on which to build and transmit the error message.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session is NULL, or `ULTRAWIDELOCK_UWB_ERR_INTERNAL` if the message could not be built.
 */
static enum ultrawidelock_uwb_err notify_error(struct ultrawidelock_uwb_session *session)
{
	struct ultrawidelock_uwb_message *message;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	message = ultrawidelock_uwb_msg_build_general_error(session,
						    ULTRAWIDELOCK_UWB_NOTIFICATION_GENERAL_ERROR_UNKNOWN);
	if (!message) {
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}
	session->transmit(message, session, session->user_data, false);
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief CCC seam callback: wrap the CCC event and forward it to the client.
 * @param event CCC event to wrap and forward.
 * @param user_data credential UWB session that owns the callback and client data.
 */
static void ultrawidelock_ccc_cb(struct cherry_ccc_event *event, void *user_data)
{
	struct ultrawidelock_uwb_session *session = user_data;
	ultrawidelock_uwb_session_cb_t callback = session->callback;
	void *client_data = session->user_data;
	struct ultrawidelock_uwb_session_event *wrapped;

	wrapped = qmalloc(sizeof(*wrapped));
	if (!wrapped) {
		LOG_ERR("ultrawidelock_ccc_cb: OOM");
		return;
	}

	wrapped->session = session;
	switch (event->type) {
	case CHERRY_CCC_EVENT_TYPE_SESSION_STATUS:
		wrapped->type = ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_STATUS;
		wrapped->data.status = event->data.status;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_ERROR:
		wrapped->type = ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_ERROR;
		wrapped->data.error = event->data.error;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT:
		wrapped->type = ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLLER_REPORT;
		wrapped->data.controller_report = event->data.controller_report;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT:
		wrapped->type = ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT;
		wrapped->data.controlee_report = event->data.controlee_report;
		break;
	case CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT:
		wrapped->type = ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT;
		wrapped->data.diagnostics = event->data.diagnostics;
		break;
	default:
		LOG_INF("ultrawidelock_ccc_cb: unknown event type %u", event->type);
		qfree(wrapped);
		return;
	}

	wrapped->cherry_event = event;

	if (event->type == CHERRY_CCC_EVENT_TYPE_SESSION_ERROR) {
		notify_error(session);
	}
	if (event->type == CHERRY_CCC_EVENT_TYPE_SESSION_STATUS &&
	    event->data.status->session_state == CHERRY_CCC_SESSION_STATE_DEINIT) {
		qfree(session->ursk); /* NULL if the destroy API already freed it */
		qfree(session);
	}

	callback(wrapped, client_data);
}

/**
 * @brief Tear down: destroy the CCC session, or free directly if there is none.
 * @param session Session to close.
 */
static void session_close(struct ultrawidelock_uwb_session *session)
{
	if (session->ccc_session) {
		cherry_ccc_session_destroy(session->ccc_session);
	} else {
		qfree(session);
	}
}

/**
 * @brief Initialize a session by creating and configuring a CCC credential responder, setting URSK,
 * protocol version, antennas, and diagnostics, then starting the session. On any error, tears down
 * the session and returns the mapped error code.
 * @param session Session to initialize.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session is NULL, or the mapped CCC error on failure.
 */
enum ultrawidelock_uwb_err ultrawidelock_uwb_session_init(struct ultrawidelock_uwb_session *session)
{
	struct ultrawidelock_uwb_adapter_reader_config *reader;
	enum cherry_err err;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}

	session->ccc_session = cherry_ccc_session_create_ultrawidelock_responder(
		session->ultrawidelock_ctx->cherry_ctx, ultrawidelock_ccc_cb, session,
		&session->ccc_ultrawidelock_config);
	if (!session->ccc_session) {
		LOG_ERR("create_ultrawidelock_responder failed");
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}

	if (session->ursk) {
		err = cherry_ccc_session_set_ursk(session->ccc_session, session->ursk);
		if (err) {
			goto fail;
		}
	}
	if (session->selected_protocol_version) {
		err = cherry_ccc_session_set_protocol_version(session->ccc_session,
							      session->selected_protocol_version);
		if (err) {
			goto fail;
		}
	}

	reader = session->ultrawidelock_ctx->config;
	if (reader->r1_antennas[0] || reader->r1_antennas[1]) {
		err = cherry_ccc_session_set_antennas(session->ccc_session, reader->r1_antennas[0],
						      reader->r1_antennas[1]);
		if (err) {
			goto fail;
		}
	}
	if (reader->r2_antennas[0] || reader->r2_antennas[1]) {
		err = cherry_ccc_session_set_round2_antennas(
			session->ccc_session, reader->r2_antennas[0], reader->r2_antennas[1]);
		if (err) {
			goto fail;
		}
	}
	if (session->ultrawidelock_ctx->diag_config) {
		err = cherry_ccc_session_set_diagnostics(session->ccc_session,
							 *session->ultrawidelock_ctx->diag_config);
		if (err) {
			goto fail;
		}
	}

	err = cherry_ccc_session_start(session->ccc_session);
	if (err) {
		goto fail;
	}
	return ULTRAWIDELOCK_UWB_ERR_NONE;

fail:
	LOG_ERR("session init step failed: %d", err);
	session_close(session);
	return cherry_err_to_ultrawidelock(err);
}

/**
 * @brief Start an active CCC session. On error, tears down the session and returns the mapped error
 * code.
 * the mapped CCC error on failure.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_start(struct ultrawidelock_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	err = cherry_ccc_session_start(session->ccc_session);
	if (err) {
		session_close(session);
		return cherry_err_to_ultrawidelock(err);
	}
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Stop an active CCC session, transitioning to SUSPENDED state. On error, tears down the
 * session and returns the mapped error code.
 * @param session Session to stop.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session is NULL, or the mapped CCC error on failure.
 */
enum ultrawidelock_uwb_err ultrawidelock_uwb_session_stop(struct ultrawidelock_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	err = cherry_ccc_session_stop(session->ccc_session);
	if (err) {
		session_close(session);
		return cherry_err_to_ultrawidelock(err);
	}
	session->state = SUSPENDED;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Allocate a credential UWB session in the CREATED state, bound to an adapter and to the
 * caller's transmit and event callbacks. No CCC session is started here.
 */
struct ultrawidelock_uwb_session *
ultrawidelock_uwb_session_create(struct ultrawidelock_uwb_adapter *ultrawidelock_ctx,
				 uint32_t session_id, ultrawidelock_uwb_session_cb_t callback,
				 ultrawidelock_uwb_adapter_transmit_message_t transmit,
				 void *user_data)
{
	struct ultrawidelock_uwb_session *session;

	if (!ultrawidelock_ctx || !transmit || !callback) {
		return NULL;
	}

	session = qcalloc(1, sizeof(*session));
	if (!session) {
		return NULL;
	}

	session->ultrawidelock_ctx = ultrawidelock_ctx;
	session->session_id = session_id;
	session->callback = callback;
	session->transmit = transmit;
	session->user_data = user_data;
	session->state = CREATED;

	LOG_INF("credential session created");
	return session;
}

/**
 * @brief Destroy a credential UWB session, freeing the URSK and tearing down the underlying CCC
 * session.
 * @param session Session to destroy; no-op if NULL.
 */
void ultrawidelock_uwb_session_destroy(struct ultrawidelock_uwb_session *session)
{
	if (!session) {
		return;
	}
	qfree(session->ursk);
	session->ursk = NULL; /* the CCC DEINIT teardown frees it too */
	session_close(session);
}

/**
 * @brief Free a session event, releasing its wrapped CCC event if present.
 */
void ultrawidelock_uwb_session_event_free(struct ultrawidelock_uwb_session_event *event)
{
	if (!event) {
		return;
	}
	if (event->cherry_event) {
		cherry_ccc_event_free(event->cherry_event);
	}
	qfree(event);
}

/**
 * @brief Store a copy of the URSK (Unique Ranging Session Key) for later use during session
 * initialization. Allocates a 16-byte buffer and returns ULTRAWIDELOCK_UWB_ERR_INTERNAL on
 * allocation failure.
 * @param session Session that receives the copied URSK.
 * @param ursk Source URSK bytes to copy.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session or ursk is NULL, or `ULTRAWIDELOCK_UWB_ERR_INTERNAL` on allocation failure.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_ursk(struct ultrawidelock_uwb_session *session, const uint8_t *ursk)
{
	if (!session || !ursk) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	session->ursk = qmalloc(ULTRAWIDELOCK_UWB_URSK_SIZE);
	if (!session->ursk) {
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}
	memcpy(session->ursk, ursk, ULTRAWIDELOCK_UWB_URSK_SIZE);
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Store the protocol version selected by the reader for later use during session
 * initialization.
 * @param session Session that receives the selected protocol version.
 * @param selected_protocol_version Protocol version chosen by the reader.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, or `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session is NULL.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_protocol_version(struct ultrawidelock_uwb_session *session,
					       uint16_t selected_protocol_version)
{
	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	session->selected_protocol_version = selected_protocol_version;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Begin session setup by building and transmitting M1, transitioning from CREATED to M1_SENT
 * state. Returns ULTRAWIDELOCK_UWB_ERR_INVALID_STATE if not in CREATED state.
 * @param session Session to begin setup on.
 * @return `ULTRAWIDELOCK_UWB_ERR_NONE` on success, `ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER` if
 * session is NULL, `ULTRAWIDELOCK_UWB_ERR_INVALID_STATE` if not in CREATED state, or
 * `ULTRAWIDELOCK_UWB_ERR_INTERNAL` if M1 could not be built.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_init_setup(struct ultrawidelock_uwb_session *session)
{
	struct ultrawidelock_uwb_message *m1;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	if (session->state != CREATED) {
		LOG_ERR("init_setup in bad state %u", session->state);
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}

	/* Only session_id is known now; the rest is filled in as M2/M4 arrive. */
	session->ccc_ultrawidelock_config.session_id = session->session_id;

	m1 = ultrawidelock_uwb_msg_build_m1(session);
	if (!m1) {
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}
	LOG_INF("Sending RangingSessionSetupM1 message");
	session->transmit(m1, session, session->user_data, true);
	session->state = M1_SENT;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Store the time offset used to synchronize clocks between reader and device.
 * @param session Session to update.
 * @param time_offset Time offset in microseconds.
 * @return ULTRAWIDELOCK_UWB_ERR_NONE on success, ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER if session
 * is NULL.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_time_offset(struct ultrawidelock_uwb_session *session,
					  int64_t time_offset)
{
	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	session->time_offset = time_offset;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Validate and dispatch an incoming credential UWB message to the appropriate protocol
 * handler.
 * @param session Session that received the message.
 * @param message Message to validate and process.
 * @return ULTRAWIDELOCK_UWB_ERR_NONE on success, ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER if session
 * or message is NULL, ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED if the message is shorter than the header
 * or the payload length does not match, ULTRAWIDELOCK_UWB_ERR_MESSAGE_UNSUPPORTED for an
 * unrecognized protocol.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_message_handle(struct ultrawidelock_uwb_session *session,
					 struct ultrawidelock_uwb_message *message)
{
	uint8_t protocol;
	uint16_t payload_length;

	if (!session || !message) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	if (message->len < ULTRAWIDELOCK_HEADER_LENGTH) {
		LOG_ERR("message shorter than header");
		return ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED;
	}

	protocol = ultrawidelock_uwb_msg_protocol_header(message->data);
	payload_length = ultrawidelock_uwb_msg_payload_length(message->data);
	if (payload_length != message->len - ULTRAWIDELOCK_HEADER_LENGTH) {
		LOG_ERR("payload length %u != actual %zu", payload_length,
			message->len - ULTRAWIDELOCK_HEADER_LENGTH);
		return ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED;
	}

	switch (protocol) {
	case ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE:
		return ultrawidelock_uwb_msg_process_ranging(session, message);
	case ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_NOTIFICATION:
		return ultrawidelock_uwb_msg_process_notification(session, message);
	case ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_SUPPLEMENTARY_SERVICE:
		return ultrawidelock_uwb_msg_process_supplementary(session, message);
	default:
		LOG_INF("protocol %u unsupported", protocol);
		return ULTRAWIDELOCK_UWB_ERR_MESSAGE_UNSUPPORTED;
	}
}

/**
 * @brief Suspend an active ranging session by sending a suspend request.
 * @param session Session to suspend.
 * @return ULTRAWIDELOCK_UWB_ERR_NONE on success, ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER if session
 * is NULL, ULTRAWIDELOCK_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is
 * not in the RANGING state, ULTRAWIDELOCK_UWB_ERR_INTERNAL if the suspend request could not be
 * built.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_suspend(struct ultrawidelock_uwb_session *session)
{
	struct ultrawidelock_uwb_message *request;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}
	if (session->state != RANGING) {
		LOG_ERR("suspend in bad state %u", session->state);
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}

	request = ultrawidelock_uwb_msg_build_suspend_resume_request(session, true);
	if (!request) {
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}
	session->transmit(request, session, session->user_data, true);
	session->state = SUSPEND_REQ_SENT;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}

/**
 * @brief Forcibly stop the active CCC session, transitioning it to SUSPENDED without a
 * request/response exchange.
 * @param session Session to force-suspend.
 * @return ULTRAWIDELOCK_UWB_ERR_NONE on success, ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER if session
 * is NULL, ULTRAWIDELOCK_UWB_ERR_INVALID_STATE if no CCC session is active, otherwise the error
 * translated from cherry_ccc_session_stop.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_forced_suspend(struct ultrawidelock_uwb_session *session)
{
	enum cherry_err err;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}
	err = cherry_ccc_session_stop(session->ccc_session);
	if (err == CHERRY_ERR_NONE) {
		session->state = SUSPENDED;
	}
	return cherry_err_to_ultrawidelock(err);
}

/**
 * @brief Resume a suspended ranging session by building and transmitting a resume request.
 * ULTRAWIDELOCK_UWB_ERR_INVALID_STATE if there is no active CCC session or the session is not in
 * the SUSPENDED state, ULTRAWIDELOCK_UWB_ERR_INTERNAL if the resume request could not be built.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_resume(struct ultrawidelock_uwb_session *session)
{
	struct ultrawidelock_uwb_message *request;

	if (!session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER;
	}
	if (!session->ccc_session) {
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}
	if (session->state != SUSPENDED) {
		LOG_ERR("resume in bad state %u", session->state);
		return ULTRAWIDELOCK_UWB_ERR_INVALID_STATE;
	}

	request = ultrawidelock_uwb_msg_build_suspend_resume_request(session, false);
	if (!request) {
		return ULTRAWIDELOCK_UWB_ERR_INTERNAL;
	}
	session->transmit(request, session, session->user_data, true);
	session->state = RESUME_REQ_SENT;
	return ULTRAWIDELOCK_UWB_ERR_NONE;
}
