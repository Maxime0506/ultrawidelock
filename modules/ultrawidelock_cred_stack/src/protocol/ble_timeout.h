/* Aliro 1.0 Bluetooth LE responseTimeout rules (section 11.9). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ultrawidelock_cred_ble_timeout_direction {
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_INCOMING = 0,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_OUTGOING = 1,
};

enum ultrawidelock_cred_ble_timeout_role {
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_IDLE = 0,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_LOCAL_TRANSMITTER,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_LOCAL_RECEIVER,
};

enum ultrawidelock_cred_ble_timeout_message {
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_UNKNOWN = 0,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS_RKE,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_AP_REQUEST,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_AP_RESPONSE,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SETUP_M1,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SETUP_M2,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SETUP_M3,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SETUP_M4,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SUSPEND_RESPONSE,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_RESUME_RESPONSE,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SETUP_LATER,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_RESUME_LATER,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_SECURE_RANGING_FAILED,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_RANGING_SUSPENDED,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_RKE_REQUEST,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_READER_STATUS_CHANGED,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_TIME_SYNC,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_PASS_THROUGH,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_BUSY,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR,
};

enum ultrawidelock_cred_ble_timeout_action {
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_NO_ACTION = 0,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_ARM,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_STOP,
	ULTRAWIDELOCK_CRED_BLE_TIMEOUT_TERMINATE,
};

/**
 * BLE timeout state machine: tracks the current role (reader or mobile) and which message (if any)
 * is pending a response. Used to enforce timeouts on authentication steps.
 */
struct ultrawidelock_cred_ble_timeout_state {
	enum ultrawidelock_cred_ble_timeout_role role;
	enum ultrawidelock_cred_ble_timeout_message pending_message;
};

/* Classify one complete, unencrypted Aliro BLE message. */
int ultrawidelock_cred_ble_timeout_classify(const uint8_t *data, size_t data_length,
				   enum ultrawidelock_cred_ble_timeout_message *message);

/* Apply Tables 11-27/11-28 to a per-connection response timer state. */
enum ultrawidelock_cred_ble_timeout_action
ultrawidelock_cred_ble_timeout_observe(struct ultrawidelock_cred_ble_timeout_state *state,
			      enum ultrawidelock_cred_ble_timeout_direction direction,
			      enum ultrawidelock_cred_ble_timeout_message message);

#ifdef __cplusplus
}
#endif
