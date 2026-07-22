/* Aliro 1.0 Bluetooth LE message framing (section 11.7). */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	WOZ_ALIRO_BLE_HEADER_SIZE = 4,
	WOZ_ALIRO_BLE_AUTH_TAG_SIZE = 16,
};

enum woz_aliro_ble_protocol {
	WOZ_ALIRO_BLE_PROTOCOL_AP = 0,
	WOZ_ALIRO_BLE_PROTOCOL_UWB = 1,
	WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION = 2,
	WOZ_ALIRO_BLE_PROTOCOL_SUPPLEMENTARY = 3,
};

enum woz_aliro_ble_notification {
	WOZ_ALIRO_BLE_NOTIFICATION_EVENT = 0,
	WOZ_ALIRO_BLE_NOTIFICATION_RANGING = 1,
	WOZ_ALIRO_BLE_NOTIFICATION_READER_STATUS_CHANGED = 2,
	WOZ_ALIRO_BLE_NOTIFICATION_ACCESS_COMPLETED = 3,
	WOZ_ALIRO_BLE_NOTIFICATION_RKE_REQUEST = 4,
	WOZ_ALIRO_BLE_NOTIFICATION_INITIATE_ACCESS = 5,
};

enum woz_aliro_ble_supplementary_service {
	WOZ_ALIRO_BLE_SUPPLEMENTARY_TIME_SYNC = 0,
};

enum woz_aliro_ble_result {
	WOZ_ALIRO_BLE_OK = 0,
	WOZ_ALIRO_BLE_INVALID_ARGUMENT = -1,
	WOZ_ALIRO_BLE_TRUNCATED = -2,
	WOZ_ALIRO_BLE_MALFORMED = -3,
	WOZ_ALIRO_BLE_BUFFER_TOO_SMALL = -4,
};

struct woz_aliro_ble_message {
	uint8_t protocol;
	uint8_t message_id;
	const uint8_t *payload;
	size_t payload_length;
};

/* Parse exactly one message at the beginning of data. consumed permits an
 * L2CAP SDU containing several concatenated Aliro messages. */
int woz_aliro_ble_parse_message(const uint8_t *data, size_t data_length,
				struct woz_aliro_ble_message *message, size_t *consumed);

int woz_aliro_ble_build_message(uint8_t protocol, uint8_t message_id, const uint8_t *payload,
				size_t payload_length, uint8_t *output, size_t output_capacity,
				size_t *output_length);

/* Parse Notification/Initiate Access Protocol and return the complete encoded
 * A5 Proprietary Information TLV carried by attribute 0. */
int woz_aliro_ble_parse_initiate_access(const struct woz_aliro_ble_message *message,
					const uint8_t **proprietary_information,
					size_t *proprietary_information_length);

/* True for messages owned by the BLE/UWB adapter after Access Protocol
 * completion. Keep this narrow so unrelated notifications and third-party
 * payloads cannot enter the ranging state machine. */
int woz_aliro_ble_is_uwb_control_message(const struct woz_aliro_ble_message *message);

/* Build the unencrypted forms. The caller applies BleSK protection to the
 * payload and adjusts the length before transmission. */
int woz_aliro_ble_build_access_completed(uint8_t reader_capabilities, uint8_t reader_state,
					 uint8_t output[8]);
int woz_aliro_ble_build_reader_status_changed(uint8_t operation_source, uint8_t reader_state,
					      uint8_t output[8]);

#ifdef __cplusplus
}
#endif
