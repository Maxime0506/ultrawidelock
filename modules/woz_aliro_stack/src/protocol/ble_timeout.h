/* Aliro 1.0 Bluetooth LE responseTimeout rules (section 11.9). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum woz_aliro_ble_timeout_direction {
	WOZ_ALIRO_BLE_TIMEOUT_INCOMING = 0,
	WOZ_ALIRO_BLE_TIMEOUT_OUTGOING = 1,
};

enum woz_aliro_ble_timeout_role {
	WOZ_ALIRO_BLE_TIMEOUT_IDLE = 0,
	WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER,
	WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER,
};

enum woz_aliro_ble_timeout_message {
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_UNKNOWN = 0,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS_RKE,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M2,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M3,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M4,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_RESPONSE,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_RESPONSE,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_LATER,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_LATER,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SECURE_RANGING_FAILED,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RANGING_SUSPENDED,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RKE_REQUEST,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_READER_STATUS_CHANGED,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_TIME_SYNC,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_PASS_THROUGH,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY,
	WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR,
};

enum woz_aliro_ble_timeout_action {
	WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION = 0,
	WOZ_ALIRO_BLE_TIMEOUT_ARM,
	WOZ_ALIRO_BLE_TIMEOUT_STOP,
	WOZ_ALIRO_BLE_TIMEOUT_TERMINATE,
};

struct woz_aliro_ble_timeout_state {
	enum woz_aliro_ble_timeout_role role;
	enum woz_aliro_ble_timeout_message pending_message;
};

/* Classify one complete, unencrypted Aliro BLE message. */
int woz_aliro_ble_timeout_classify(const uint8_t *data, size_t data_length,
				   enum woz_aliro_ble_timeout_message *message);

/* Apply Tables 11-27/11-28 to a per-connection response timer state. */
enum woz_aliro_ble_timeout_action
woz_aliro_ble_timeout_observe(struct woz_aliro_ble_timeout_state *state,
			      enum woz_aliro_ble_timeout_direction direction,
			      enum woz_aliro_ble_timeout_message message);

#ifdef __cplusplus
}
#endif
