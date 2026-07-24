#include "ble_timeout.h"

#include "ble_message.h"

static int classify_attribute(const struct woz_aliro_ble_message *message,
			      enum woz_aliro_ble_timeout_message *kind)
{
	if (message->payload_length < 2) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}
	const uint8_t attribute_id = message->payload[0];
	const size_t attribute_length = message->payload[1];
	if (attribute_length + 2 != message->payload_length) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}

	if (message->message_id == WOZ_ALIRO_BLE_NOTIFICATION_EVENT) {
		if (attribute_id == 0 && attribute_length == 0) {
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY;
		} else if (attribute_id == 1 && attribute_length == 1) {
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR;
		}
		return WOZ_ALIRO_BLE_OK;
	}
	if (message->message_id != WOZ_ALIRO_BLE_NOTIFICATION_RANGING) {
		return WOZ_ALIRO_BLE_OK;
	}

	switch (attribute_id) {
	case 0:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING;
		break;
	case 1:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME;
		break;
	case 2:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_LATER;
		break;
	case 3:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_LATER;
		break;
	case 4:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SECURE_RANGING_FAILED;
		break;
	case 5:
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RANGING_SUSPENDED;
		break;
	default:
		break;
	}
	return WOZ_ALIRO_BLE_OK;
}

int woz_aliro_ble_timeout_classify(const uint8_t *data, size_t data_length,
				   enum woz_aliro_ble_timeout_message *kind)
{
	struct woz_aliro_ble_message message;
	size_t consumed = 0;
	if (kind == NULL) {
		return WOZ_ALIRO_BLE_INVALID_ARGUMENT;
	}
	*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_UNKNOWN;
	const int status = woz_aliro_ble_parse_message(data, data_length, &message, &consumed);
	if (status != WOZ_ALIRO_BLE_OK) {
		return status;
	}
	if (consumed != data_length) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}

	switch (message.protocol) {
	case WOZ_ALIRO_BLE_PROTOCOL_AP:
		if (message.message_id == 0) {
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST;
		} else if (message.message_id == 1) {
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE;
		}
		return WOZ_ALIRO_BLE_OK;
	case WOZ_ALIRO_BLE_PROTOCOL_UWB:
		if (message.message_id <= 7) {
			*kind = (enum woz_aliro_ble_timeout_message)(
				WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1 + message.message_id);
		}
		return WOZ_ALIRO_BLE_OK;
	case WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION:
		switch (message.message_id) {
		case WOZ_ALIRO_BLE_NOTIFICATION_EVENT:
		case WOZ_ALIRO_BLE_NOTIFICATION_RANGING:
			return classify_attribute(&message, kind);
		case WOZ_ALIRO_BLE_NOTIFICATION_READER_STATUS_CHANGED:
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_READER_STATUS_CHANGED;
			break;
		case WOZ_ALIRO_BLE_NOTIFICATION_ACCESS_COMPLETED:
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED;
			break;
		case WOZ_ALIRO_BLE_NOTIFICATION_RKE_REQUEST:
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RKE_REQUEST;
			break;
		case WOZ_ALIRO_BLE_NOTIFICATION_INITIATE_ACCESS:
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS;
			break;
		case 6:
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS_RKE;
			break;
		default:
			break;
		}
		return WOZ_ALIRO_BLE_OK;
	case WOZ_ALIRO_BLE_PROTOCOL_SUPPLEMENTARY:
		if (message.message_id == WOZ_ALIRO_BLE_SUPPLEMENTARY_TIME_SYNC) {
			*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_TIME_SYNC;
		}
		return WOZ_ALIRO_BLE_OK;
	case 4: /* Third Party App / Pass Through. */
		*kind = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_PASS_THROUGH;
		return WOZ_ALIRO_BLE_OK;
	default:
		return WOZ_ALIRO_BLE_OK;
	}
}

static int has_response_timeout(enum woz_aliro_ble_timeout_message message)
{
	return message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS_RKE ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1 ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M2 ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M3 ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME ||
	       message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST;
}

static int is_allowed_reply(enum woz_aliro_ble_timeout_message request,
			    enum woz_aliro_ble_timeout_message reply)
{
	if (reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY ||
	    reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR) {
		return 1;
	}
	switch (request) {
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS:
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS_RKE:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST ||
		       reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1 ||
		       reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_LATER;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M2 ||
		       reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_LATER;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M2:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M3;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M3:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M4;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_RESPONSE;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST ||
		       reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_LATER;
	case WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST:
		return reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_RESPONSE ||
		       reply == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_LATER;
	default:
		return 0;
	}
}

static int collision_replaces_pending(enum woz_aliro_ble_timeout_message pending,
				      enum woz_aliro_ble_timeout_message incoming)
{
	if (pending == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST) {
		return incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING ||
		       incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME ||
		       incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST;
	}
	if (pending == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST) {
		return incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING ||
		       incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME ||
		       incoming == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST;
	}
	return 0;
}

static void set_pending(struct woz_aliro_ble_timeout_state *state,
			enum woz_aliro_ble_timeout_direction direction,
			enum woz_aliro_ble_timeout_message message)
{
	state->role = direction == WOZ_ALIRO_BLE_TIMEOUT_OUTGOING
			      ? WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER
			      : WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER;
	state->pending_message = message;
}

static void clear_pending(struct woz_aliro_ble_timeout_state *state)
{
	state->role = WOZ_ALIRO_BLE_TIMEOUT_IDLE;
	state->pending_message = WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_UNKNOWN;
}

enum woz_aliro_ble_timeout_action
woz_aliro_ble_timeout_observe(struct woz_aliro_ble_timeout_state *state,
			      enum woz_aliro_ble_timeout_direction direction,
			      enum woz_aliro_ble_timeout_message message)
{
	if (state == NULL || (direction != WOZ_ALIRO_BLE_TIMEOUT_INCOMING &&
			      direction != WOZ_ALIRO_BLE_TIMEOUT_OUTGOING)) {
		return WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION;
	}

	if (message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY) {
		const int matching_direction =
			(direction == WOZ_ALIRO_BLE_TIMEOUT_INCOMING &&
			 state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER) ||
			(direction == WOZ_ALIRO_BLE_TIMEOUT_OUTGOING &&
			 state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER);
		return matching_direction ? WOZ_ALIRO_BLE_TIMEOUT_ARM
					  : WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION;
	}

	if (message == WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR) {
		if (direction == WOZ_ALIRO_BLE_TIMEOUT_INCOMING &&
		    state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER) {
			clear_pending(state);
			return WOZ_ALIRO_BLE_TIMEOUT_TERMINATE;
		}
		if (direction == WOZ_ALIRO_BLE_TIMEOUT_OUTGOING &&
		    state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER) {
			clear_pending(state);
			return WOZ_ALIRO_BLE_TIMEOUT_STOP;
		}
		return WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION;
	}

	if (state->role != WOZ_ALIRO_BLE_TIMEOUT_IDLE) {
		const int reply_direction_matches =
			(direction == WOZ_ALIRO_BLE_TIMEOUT_INCOMING &&
			 state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER) ||
			(direction == WOZ_ALIRO_BLE_TIMEOUT_OUTGOING &&
			 state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER);
		if (reply_direction_matches && is_allowed_reply(state->pending_message, message)) {
			if (has_response_timeout(message)) {
				set_pending(state, direction, message);
				return WOZ_ALIRO_BLE_TIMEOUT_ARM;
			}
			clear_pending(state);
			return WOZ_ALIRO_BLE_TIMEOUT_STOP;
		}
		if (direction == WOZ_ALIRO_BLE_TIMEOUT_INCOMING &&
		    state->role == WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER &&
		    collision_replaces_pending(state->pending_message, message)) {
			set_pending(state, direction, message);
			return WOZ_ALIRO_BLE_TIMEOUT_ARM;
		}
		return WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION;
	}

	if (has_response_timeout(message)) {
		set_pending(state, direction, message);
		return WOZ_ALIRO_BLE_TIMEOUT_ARM;
	}
	return WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION;
}
