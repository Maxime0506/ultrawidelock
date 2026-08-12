/** @file ultrawidelock_uwb_session.h — per-session public interface. */

#pragma once

#include <ultrawidelock_uwb_adapter/ultrawidelock_uwb_adapter.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque per-session context. */
struct ultrawidelock_uwb_session;

/**
 * @brief Framed credential BLE message with 4-byte header followed by TLV payload.
 * @param len Number of valid bytes in @p data.
 * @param data Message bytes (4-byte header followed by TLV attributes).
 */
struct ultrawidelock_uwb_message {
	/** Number of valid bytes in @p data. */
	size_t len;
	/** Message bytes (4-byte header followed by TLV attributes). */
	uint8_t data[];
};

/** Session event kinds (aliased to the CCC event types). */
enum ultrawidelock_uwb_session_event_type {
	ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_STATUS = CHERRY_CCC_EVENT_TYPE_SESSION_STATUS,
	ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_ERROR = CHERRY_CCC_EVENT_TYPE_SESSION_ERROR,
	ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLLER_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLLER_REPORT,
	ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_CONTROLEE_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_CONTROLEE_REPORT,
	ULTRAWIDELOCK_UWB_SESSION_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT =
		CHERRY_CCC_EVENT_TYPE_SESSION_DIAGNOSTIC_REPORT,
};

/**
 * @brief Session event handed to the client, carrying status, error, or distance reports.
 * @param session Opaque per-session context.
 * @param type Event type (status, error, controller report, controlee report, or diagnostics).
 * @param cherry_event Underlying Cherry event object.
 * @param data Union holding the event payload: session status, error code and diagnostic context,
 * controller distance and timestamp estimate, controlee distance and timestamp estimate, or
 * diagnostic report snapshot.
 */
struct ultrawidelock_uwb_session_event {
	/**
	 * @brief Opaque per-session context.
	 */
	struct ultrawidelock_uwb_session *session;
	enum ultrawidelock_uwb_session_event_type type;
	void *cherry_event;
	union {
		/**
		 * @brief Session status report (active, paused, stopped) and update reason
		 * (initiation, measurement update, termination).
		 */
		struct cherry_ccc_session_event_session_status *status;
		/**
		 * @brief Session error payload carrying error code and diagnostic context.
		 */
		struct cherry_ccc_session_event_error *error;
		/**
		 * @brief Controller (phone) session report carrying its distance and timestamp
		 * estimate.
		 */
		struct cherry_ccc_controller_session_report *controller_report;
		/**
		 * @brief Controlee (lock) session report carrying its distance and timestamp
		 * estimate.
		 */
		struct cherry_ccc_controlee_session_report *controlee_report;
		/**
		 * @brief Diagnostic report snapshot (ranging samples, signal strength, time sync)
		 * for this update.
		 */
		struct cherry_common_diag_report *diagnostics;
	} data;
};

/** Session notification callback type. */
typedef void (*ultrawidelock_uwb_session_cb_t)(struct ultrawidelock_uwb_session_event *event,
					       void *user_data);

/** BLE transmit callback: sends an adapter-built message to the peer. */
typedef void (*ultrawidelock_uwb_adapter_transmit_message_t)(
	struct ultrawidelock_uwb_message *message, struct ultrawidelock_uwb_session *session,
	void *user_data, bool timeout);

/**
 * @brief Create a session in the CREATED state. No CCC session is started here.
 * @param ultrawidelock_ctx Adapter supplying the Cherry context and reader configuration.
 * @param session_id Session identifier carried in the ranging-service messages.
 * @param callback Session event callback; must not be NULL.
 * @param transmit Message transmit callback; must not be NULL.
 * @param user_data Opaque pointer passed back to the callbacks.
 * @return New session, or NULL on bad parameters or allocation failure.
 */
struct ultrawidelock_uwb_session *
ultrawidelock_uwb_session_create(struct ultrawidelock_uwb_adapter *ultrawidelock_ctx, uint32_t session_id,
				 ultrawidelock_uwb_session_cb_t callback,
				 ultrawidelock_uwb_adapter_transmit_message_t transmit,
				 void *user_data);

/** Stop if needed and release a session. */
void ultrawidelock_uwb_session_destroy(struct ultrawidelock_uwb_session *session);

/** Free a message produced by the adapter's transmit callback. */
void ultrawidelock_uwb_session_message_free(struct ultrawidelock_uwb_message *message);

/**
 * @brief Free an event delivered to the session callback.
 * @param event Event to free.
 */
void ultrawidelock_uwb_session_event_free(struct ultrawidelock_uwb_session_event *event);

/** Provide the 32-byte URSK for the session. */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_ursk(struct ultrawidelock_uwb_session *session, const uint8_t *ursk);

/** Record the selected BLE/UWB protocol version. */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_protocol_version(struct ultrawidelock_uwb_session *session,
					       uint16_t selected_protocol_version);

/** Begin setup: build M1 and transmit it to the user device. */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_init_setup(struct ultrawidelock_uwb_session *session);

/** Set the time offset (microseconds) added to the negotiated UWB_Time0. */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_set_time_offset(struct ultrawidelock_uwb_session *session,
					  int64_t time_offset);

/** Handle an incoming BLE message (M2/M4, suspend/resume, notifications). */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_message_handle(struct ultrawidelock_uwb_session *session,
					 struct ultrawidelock_uwb_message *message);

/** Request a graceful suspend (builds and sends a suspend request). */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_suspend(struct ultrawidelock_uwb_session *session);

/** Suspend ranging immediately without waiting for a peer response. */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_forced_suspend(struct ultrawidelock_uwb_session *session);

/**
 * @brief Build and send a resume request for a suspended credential UWB ranging session.
 * @param session credential UWB session to resume.
 * @return credential UWB error code indicating success or failure of the resume request.
 */
enum ultrawidelock_uwb_err
ultrawidelock_uwb_session_resume(struct ultrawidelock_uwb_session *session);

#ifdef __cplusplus
}
#endif
