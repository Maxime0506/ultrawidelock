#include "ble_message.h"

#include "tlv.h"

#include <string.h>

int woz_aliro_ble_parse_message(const uint8_t *data, size_t data_length,
				struct woz_aliro_ble_message *message, size_t *consumed)
{
	if (data == NULL || message == NULL || consumed == NULL) {
		return WOZ_ALIRO_BLE_INVALID_ARGUMENT;
	}
	if (data_length < WOZ_ALIRO_BLE_HEADER_SIZE) {
		return WOZ_ALIRO_BLE_TRUNCATED;
	}
	if ((data[0] & 0xc0u) != 0) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}
	const size_t payload_length = ((size_t)data[2] << 8) | data[3];
	if (payload_length == 0) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}
	if (payload_length > data_length - WOZ_ALIRO_BLE_HEADER_SIZE) {
		return WOZ_ALIRO_BLE_TRUNCATED;
	}
	message->protocol = data[0];
	message->message_id = data[1];
	message->payload = data + WOZ_ALIRO_BLE_HEADER_SIZE;
	message->payload_length = payload_length;
	*consumed = WOZ_ALIRO_BLE_HEADER_SIZE + payload_length;
	return WOZ_ALIRO_BLE_OK;
}

int woz_aliro_ble_build_message(uint8_t protocol, uint8_t message_id, const uint8_t *payload,
				size_t payload_length, uint8_t *output, size_t output_capacity,
				size_t *output_length)
{
	if (output == NULL || output_length == NULL || payload == NULL || (protocol & 0xc0u) != 0 ||
	    payload_length == 0 || payload_length > UINT16_MAX) {
		return WOZ_ALIRO_BLE_INVALID_ARGUMENT;
	}
	if (output_capacity < WOZ_ALIRO_BLE_HEADER_SIZE ||
	    payload_length > output_capacity - WOZ_ALIRO_BLE_HEADER_SIZE) {
		return WOZ_ALIRO_BLE_BUFFER_TOO_SMALL;
	}
	output[0] = protocol;
	output[1] = message_id;
	output[2] = (uint8_t)(payload_length >> 8);
	output[3] = (uint8_t)payload_length;
	memcpy(output + WOZ_ALIRO_BLE_HEADER_SIZE, payload, payload_length);
	*output_length = WOZ_ALIRO_BLE_HEADER_SIZE + payload_length;
	return WOZ_ALIRO_BLE_OK;
}

int woz_aliro_ble_parse_initiate_access(const struct woz_aliro_ble_message *message,
					const uint8_t **proprietary_information,
					size_t *proprietary_information_length)
{
	if (message == NULL || proprietary_information == NULL ||
	    proprietary_information_length == NULL || message->payload == NULL) {
		return WOZ_ALIRO_BLE_INVALID_ARGUMENT;
	}
	if (message->protocol != WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION ||
	    message->message_id != WOZ_ALIRO_BLE_NOTIFICATION_INITIATE_ACCESS ||
	    message->payload_length < 3 || message->payload[0] != 0 || message->payload[1] == 0 ||
	    (size_t)message->payload[1] + 2 != message->payload_length) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}
	const uint8_t *encoded = message->payload + 2;
	const size_t encoded_length = message->payload[1];
	struct woz_aliro_tlv tlv;
	size_t offset = 0;
	if (woz_aliro_tlv_next(encoded, encoded_length, &offset, &tlv) != WOZ_ALIRO_TLV_OK ||
	    tlv.tag != 0xa5 || offset != encoded_length) {
		return WOZ_ALIRO_BLE_MALFORMED;
	}
	*proprietary_information = encoded;
	*proprietary_information_length = encoded_length;
	return WOZ_ALIRO_BLE_OK;
}

int woz_aliro_ble_is_uwb_control_message(const struct woz_aliro_ble_message *message)
{
	if (message == NULL) {
		return 0;
	}
	return message->protocol == WOZ_ALIRO_BLE_PROTOCOL_UWB ||
	       (message->protocol == WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION &&
		message->message_id == WOZ_ALIRO_BLE_NOTIFICATION_RANGING) ||
	       (message->protocol == WOZ_ALIRO_BLE_PROTOCOL_SUPPLEMENTARY &&
		message->message_id == WOZ_ALIRO_BLE_SUPPLEMENTARY_TIME_SYNC);
}

static int build_status(uint8_t message_id, uint8_t first, uint8_t reader_state, uint8_t output[8])
{
	if (output == NULL) {
		return WOZ_ALIRO_BLE_INVALID_ARGUMENT;
	}
	output[0] = WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION;
	output[1] = message_id;
	output[2] = 0;
	output[3] = 4;
	output[4] = 0; /* State/Reader Information attribute ID. */
	output[5] = 2;
	output[6] = first;
	output[7] = reader_state;
	return WOZ_ALIRO_BLE_OK;
}

int woz_aliro_ble_build_access_completed(uint8_t reader_capabilities, uint8_t reader_state,
					 uint8_t output[8])
{
	return build_status(WOZ_ALIRO_BLE_NOTIFICATION_ACCESS_COMPLETED, reader_capabilities,
			    reader_state, output);
}

int woz_aliro_ble_build_reader_status_changed(uint8_t operation_source, uint8_t reader_state,
					      uint8_t output[8])
{
	return build_status(WOZ_ALIRO_BLE_NOTIFICATION_READER_STATUS_CHANGED, operation_source,
			    reader_state, output);
}
