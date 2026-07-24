#include "test.h"

#include "ble_message.h"
#include "ble_timeout.h"
#include "nfc_select.h"
#include "tlv.h"

#include <stdint.h>
#include <string.h>

void test_aliro_ble(void)
{
	/* Successful nRF/iPhone capture, 2026-07-20. This is the clear first
	 * message; credential and randomized encrypted material are intentionally
	 * excluded from the fixture. */
	static const uint8_t initiate_access[] = {
		0x02, 0x05, 0x00, 0x19, 0x00, 0x17, 0xa5, 0x15,
		0x80, 0x02, 0x00, 0x00, 0x5c, 0x04, 0x01, 0x00,
		0x00, 0x09, 0x7f, 0x66, 0x08, 0x02, 0x02, 0x0e,
		0x00, 0x02, 0x02, 0x0e, 0x00,
	};
	struct woz_aliro_ble_message message;
	size_t consumed = 0;
	T_EQ("parse captured initiate", woz_aliro_ble_parse_message(initiate_access,
		sizeof(initiate_access), &message, &consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("initiate consumed", consumed, sizeof(initiate_access));
	T_EQ("initiate protocol", message.protocol, WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION);
	T_EQ("initiate message ID", message.message_id, WOZ_ALIRO_BLE_NOTIFICATION_INITIATE_ACCESS);
	const uint8_t *proprietary = NULL;
	size_t proprietary_length = 0;
	T_EQ("parse initiate attribute", woz_aliro_ble_parse_initiate_access(&message,
		&proprietary, &proprietary_length), WOZ_ALIRO_BLE_OK);
	T_EQ("captured proprietary length", proprietary_length, 23);
	struct woz_aliro_select_response selected;
	T_EQ("parse BLE proprietary", woz_aliro_parse_proprietary_information(proprietary,
		proprietary_length, WOZ_ALIRO_SELECT_EXPEDITED, &selected), WOZ_ALIRO_SELECT_OK);
	T_EQ("BLE selected version", selected.selected_protocol_version, 0x0100);
	T_EQ("BLE max command", selected.max_command_data_length, 0x0e00);
	T_EQ("BLE max response", selected.max_response_data_length, 0x0e00);

	/* Golden plaintext UWB messages recovered at the proprietary-stack/UWB
	 * boundary in the same successful unlock. */
	static const uint8_t m1[] = {
		0x01, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00, 0x00,
		0x01, 0x01, 0x00, 0x03, 0x01, 0x03, 0x02, 0x04,
		0x81, 0xe2, 0x9c, 0x6f,
	};
	static const uint8_t m2[] = {
		0x01, 0x01, 0x00, 0x19, 0x00, 0x02, 0x00, 0x00,
		0x01, 0x01, 0x00, 0x03, 0x01, 0x02, 0x06, 0x04,
		0x00, 0x00, 0x0f, 0x00, 0x04, 0x01, 0x02, 0x05,
		0x01, 0x7c, 0x08, 0x01, 0xd0,
	};
	static const uint8_t m3[] = {
		0x01, 0x02, 0x00, 0x18, 0x04, 0x01, 0x02, 0x09,
		0x01, 0x06, 0x0a, 0x01, 0x01, 0x0b, 0x01, 0x0c,
		0x06, 0x04, 0x00, 0x00, 0x0f, 0x00, 0x08, 0x01,
		0x80, 0x0f, 0x01, 0x00,
	};
	T_EQ("parse captured M1", woz_aliro_ble_parse_message(m1, sizeof(m1), &message,
		&consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("M1 message ID", message.message_id, 0);
	T_EQ("parse captured M2", woz_aliro_ble_parse_message(m2, sizeof(m2), &message,
		&consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("M2 message ID", message.message_id, 1);
	T_EQ("parse captured M3", woz_aliro_ble_parse_message(m3, sizeof(m3), &message,
		&consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("M3 message ID", message.message_id, 2);
	T_OK("route UWB ranging service", woz_aliro_ble_is_uwb_control_message(&message));
	message.protocol = WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION;
	message.message_id = WOZ_ALIRO_BLE_NOTIFICATION_RANGING;
	T_OK("route ranging notification", woz_aliro_ble_is_uwb_control_message(&message));
	message.protocol = WOZ_ALIRO_BLE_PROTOCOL_SUPPLEMENTARY;
	message.message_id = WOZ_ALIRO_BLE_SUPPLEMENTARY_TIME_SYNC;
	T_OK("route time sync", woz_aliro_ble_is_uwb_control_message(&message));
	message.message_id = 1;
	T_OK("reject unknown supplementary", !woz_aliro_ble_is_uwb_control_message(&message));
	T_OK("reject null UWB control", !woz_aliro_ble_is_uwb_control_message(NULL));

	uint8_t status[8];
	static const uint8_t expected_changed[] = { 2, 2, 0, 4, 0, 2, 4, 0x81 };
	T_EQ("build captured reader state", woz_aliro_ble_build_reader_status_changed(4,
		0x81, status), WOZ_ALIRO_BLE_OK);
	T_OK("reader state bytes", memcmp(status, expected_changed, sizeof(status)) == 0);
	static const uint8_t expected_completed[] = { 2, 3, 0, 4, 0, 2, 0x40, 0 };
	T_EQ("build access completed", woz_aliro_ble_build_access_completed(0x40, 0,
		status), WOZ_ALIRO_BLE_OK);
	T_OK("access completed bytes", memcmp(status, expected_completed, sizeof(status)) == 0);

	uint8_t concatenated[sizeof(m1) + sizeof(m3)];
	memcpy(concatenated, m1, sizeof(m1));
	memcpy(concatenated + sizeof(m1), m3, sizeof(m3));
	T_EQ("parse first concatenated", woz_aliro_ble_parse_message(concatenated,
		sizeof(concatenated), &message, &consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("first concatenated length", consumed, sizeof(m1));
	T_EQ("reject truncated message", woz_aliro_ble_parse_message(m2, sizeof(m2) - 1,
		&message, &consumed), WOZ_ALIRO_BLE_TRUNCATED);
	static const uint8_t empty[] = { 2, 1, 0, 0 };
	T_EQ("reject empty payload", woz_aliro_ble_parse_message(empty, sizeof(empty),
		&message, &consumed), WOZ_ALIRO_BLE_MALFORMED);

	/* Section 11.9 responseTimeout chains. A reply can itself be a timed
	 * message, so AP and UWB setup alternate which peer owes the next reply. */
	enum woz_aliro_ble_timeout_message timeout_message;
	T_EQ("classify initiate timeout", woz_aliro_ble_timeout_classify(initiate_access,
		sizeof(initiate_access), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("initiate timeout kind", timeout_message,
		WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS);

	static const uint8_t ap_request[] = { 0, 0, 0, 2, 0x80, 0x80 };
	static const uint8_t ap_response[] = { 0, 1, 0, 2, 0x90, 0x00 };
	static const uint8_t busy[] = { 2, 0, 0, 2, 0, 0 };
	static const uint8_t general_error[] = { 2, 0, 0, 3, 1, 1, 0 };
	static const uint8_t access_completed[] = { 2, 3, 0, 2, 0, 0 };
	T_EQ("classify AP request", woz_aliro_ble_timeout_classify(ap_request,
		sizeof(ap_request), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("AP request kind", timeout_message, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST);

	struct woz_aliro_ble_timeout_state timeout = { 0 };
	T_EQ("receive initiate arms", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_ACCESS),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("reader owes AUTH0", timeout.role, WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER);
	T_EQ("AUTH0 restarts as transmitter", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("reader awaits AP response", timeout.role,
		WOZ_ALIRO_BLE_TIMEOUT_LOCAL_TRANSMITTER);
	T_EQ("received Busy restarts", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("AP response transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("reader owes next AP command", timeout.role,
		WOZ_ALIRO_BLE_TIMEOUT_LOCAL_RECEIVER);
	T_EQ("next AP request transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("second AP response transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_RESPONSE),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("access completed stops", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED),
		WOZ_ALIRO_BLE_TIMEOUT_STOP);
	T_EQ("AP timeout idle", timeout.role, WOZ_ALIRO_BLE_TIMEOUT_IDLE);

	static const uint8_t init_ranging[] = { 2, 1, 0, 2, 0, 0 };
	static const uint8_t ranging_suspended[] = { 2, 1, 0, 2, 5, 0 };
	static const uint8_t m4[] = { 1, 3, 0, 1, 0 };
	static const uint8_t time_sync[] = { 3, 0, 0, 1, 0 };
	T_EQ("classify ranging initiation", woz_aliro_ble_timeout_classify(init_ranging,
		sizeof(init_ranging), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("ranging initiation kind", timeout_message,
		WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING);
	T_EQ("receive ranging initiation arms", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("send M1 transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M1),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("duplicate initiation does not replace M1", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING),
		WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION);
	T_EQ("receive M2 transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M2),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("send M3 transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M3),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("Time Sync leaves timer alone", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_TIME_SYNC),
		WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION);
	T_EQ("receive M4 stops", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M4),
		WOZ_ALIRO_BLE_TIMEOUT_STOP);

	T_EQ("resume request arms", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("suspended notification is ignored while resuming",
		woz_aliro_ble_timeout_observe(&timeout, WOZ_ALIRO_BLE_TIMEOUT_INCOMING,
			WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RANGING_SUSPENDED),
		WOZ_ALIRO_BLE_TIMEOUT_NO_ACTION);
	T_EQ("incoming suspend replaces resume", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("suspend response stops", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SUSPEND_RESPONSE),
		WOZ_ALIRO_BLE_TIMEOUT_STOP);
	T_EQ("resume request rearms", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("incoming resume initiation replaces request",
		woz_aliro_ble_timeout_observe(&timeout, WOZ_ALIRO_BLE_TIMEOUT_INCOMING,
			WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_INITIATE_RANGING_RESUME),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("send Busy keeps receiver obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("send resumed request transfers obligation", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("resume response stops", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RESUME_RESPONSE),
		WOZ_ALIRO_BLE_TIMEOUT_STOP);

	T_EQ("request arms before general error", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_OUTGOING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_AP_REQUEST),
		WOZ_ALIRO_BLE_TIMEOUT_ARM);
	T_EQ("received general error terminates", woz_aliro_ble_timeout_observe(&timeout,
		WOZ_ALIRO_BLE_TIMEOUT_INCOMING, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR),
		WOZ_ALIRO_BLE_TIMEOUT_TERMINATE);
	T_EQ("general error clears timeout", timeout.role, WOZ_ALIRO_BLE_TIMEOUT_IDLE);

	T_EQ("classify Busy", woz_aliro_ble_timeout_classify(busy, sizeof(busy),
		&timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("Busy kind", timeout_message, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_BUSY);
	T_EQ("classify General Error", woz_aliro_ble_timeout_classify(general_error,
		sizeof(general_error), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("General Error kind", timeout_message,
		WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_GENERAL_ERROR);
	T_EQ("classify Access Completed", woz_aliro_ble_timeout_classify(access_completed,
		sizeof(access_completed), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("Access Completed kind", timeout_message,
		WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_ACCESS_COMPLETED);
	T_EQ("classify M4", woz_aliro_ble_timeout_classify(m4, sizeof(m4),
		&timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("M4 kind", timeout_message, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_SETUP_M4);
	T_EQ("classify Time Sync", woz_aliro_ble_timeout_classify(time_sync, sizeof(time_sync),
		&timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("Time Sync kind", timeout_message, WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_TIME_SYNC);
	T_EQ("classify suspended", woz_aliro_ble_timeout_classify(ranging_suspended,
		sizeof(ranging_suspended), &timeout_message), WOZ_ALIRO_BLE_OK);
	T_EQ("suspended kind", timeout_message,
		WOZ_ALIRO_BLE_TIMEOUT_MESSAGE_RANGING_SUSPENDED);

	/* TLV codec corners: DER high-tag-number and long-length forms, both
	 * directions. */
	struct woz_aliro_tlv tlv;
	size_t tlv_offset = 0;
	T_EQ("TLV rejects null data", woz_aliro_tlv_next(NULL, 4, &tlv_offset, &tlv),
		WOZ_ALIRO_TLV_INVALID);
	static const uint8_t tlv_single[] = { 0x80, 0x01, 0xaa };
	tlv_offset = sizeof(tlv_single);
	T_EQ("TLV end of buffer", woz_aliro_tlv_next(tlv_single, sizeof(tlv_single),
		&tlv_offset, &tlv), WOZ_ALIRO_TLV_END);
	static const uint8_t tag_truncated[] = { 0x5f };
	tlv_offset = 0;
	T_EQ("TLV rejects truncated high tag", woz_aliro_tlv_next(tag_truncated,
		sizeof(tag_truncated), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t tag_too_wide[] = { 0x5f, 0x81, 0x82, 0x83, 0x84, 0x01, 0x00 };
	tlv_offset = 0;
	T_EQ("TLV rejects five-octet tag", woz_aliro_tlv_next(tag_too_wide,
		sizeof(tag_too_wide), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t tag_zero_group[] = { 0x5f, 0x80, 0x66, 0x01, 0xaa };
	tlv_offset = 0;
	T_EQ("TLV rejects zero tag group", woz_aliro_tlv_next(tag_zero_group,
		sizeof(tag_zero_group), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t missing_length[] = { 0x80 };
	tlv_offset = 0;
	T_EQ("TLV rejects missing length", woz_aliro_tlv_next(missing_length,
		sizeof(missing_length), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t indefinite_length[] = { 0x80, 0x80, 0xaa };
	tlv_offset = 0;
	T_EQ("TLV rejects indefinite length", woz_aliro_tlv_next(indefinite_length,
		sizeof(indefinite_length), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t length_too_wide[] = { 0x80, 0x89, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	tlv_offset = 0;
	T_EQ("TLV rejects nine-octet length", woz_aliro_tlv_next(length_too_wide,
		sizeof(length_too_wide), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);
	static const uint8_t length_leading_zero[] = { 0x80, 0x82, 0x00, 0x80 };
	tlv_offset = 0;
	T_EQ("TLV rejects zero length octet", woz_aliro_tlv_next(length_leading_zero,
		sizeof(length_leading_zero), &tlv_offset, &tlv), WOZ_ALIRO_TLV_INVALID);

	T_EQ("TLV two-byte tag size", woz_aliro_tlv_encoded_size(0x7f66, 4), 7);
	T_EQ("TLV three-byte tag size", woz_aliro_tlv_encoded_size(0xbf8177, 4), 8);
	T_EQ("TLV four-byte tag unsupported", woz_aliro_tlv_encoded_size(0x01000000, 4), 0);
	T_EQ("TLV one-octet length form size", woz_aliro_tlv_encoded_size(0x80, 200), 203);
	T_EQ("TLV two-octet length form size", woz_aliro_tlv_encoded_size(0x80, 300), 304);
	T_EQ("TLV oversize length unsupported", woz_aliro_tlv_encoded_size(0x80, 0x10000), 0);

	uint8_t tlv_frame[600];
	uint8_t tlv_value[300];
	memset(tlv_value, 0x5a, sizeof(tlv_value));
	tlv_offset = 0;
	T_EQ("TLV write rejects null output", woz_aliro_tlv_write(NULL, sizeof(tlv_frame),
		&tlv_offset, 0x80, tlv_value, 1), WOZ_ALIRO_TLV_INVALID);
	T_EQ("TLV write rejects short buffer", woz_aliro_tlv_write(tlv_frame, 4,
		&tlv_offset, 0x7f66, tlv_value, 4), WOZ_ALIRO_TLV_INVALID);
	T_EQ("TLV write rejects wide tag", woz_aliro_tlv_write(tlv_frame, sizeof(tlv_frame),
		&tlv_offset, 0x01000000, tlv_value, 4), WOZ_ALIRO_TLV_INVALID);
	T_EQ("TLV write one-octet length form", woz_aliro_tlv_write(tlv_frame,
		sizeof(tlv_frame), &tlv_offset, 0x7f66, tlv_value, 200), WOZ_ALIRO_TLV_OK);
	T_EQ("TLV write two-octet length form", woz_aliro_tlv_write(tlv_frame,
		sizeof(tlv_frame), &tlv_offset, 0x80, tlv_value, 300), WOZ_ALIRO_TLV_OK);
	T_EQ("TLV write empty value", woz_aliro_tlv_write(tlv_frame, sizeof(tlv_frame),
		&tlv_offset, 0x80, NULL, 0), WOZ_ALIRO_TLV_OK);
	size_t tlv_reparse = 0;
	T_EQ("TLV round-trip first", woz_aliro_tlv_next(tlv_frame, tlv_offset, &tlv_reparse,
		&tlv), WOZ_ALIRO_TLV_OK);
	T_EQ("TLV round-trip two-byte tag", tlv.tag, 0x7f66);
	T_EQ("TLV round-trip 200-byte value", tlv.length, 200);
	T_EQ("TLV round-trip second", woz_aliro_tlv_next(tlv_frame, tlv_offset, &tlv_reparse,
		&tlv), WOZ_ALIRO_TLV_OK);
	T_EQ("TLV round-trip 300-byte value", tlv.length, 300);
	T_EQ("TLV round-trip third", woz_aliro_tlv_next(tlv_frame, tlv_offset, &tlv_reparse,
		&tlv), WOZ_ALIRO_TLV_OK);
	T_EQ("TLV round-trip empty value", tlv.length, 0);
	T_EQ("TLV round-trip consumes exactly", tlv_reparse, tlv_offset);

	/* Framing builder and header validation. */
	uint8_t frame[16];
	size_t frame_length = 0;
	static const uint8_t frame_payload[] = { 0xde, 0xad, 0xbe, 0xef };
	T_EQ("build BLE frame", woz_aliro_ble_build_message(WOZ_ALIRO_BLE_PROTOCOL_UWB, 2,
		frame_payload, sizeof(frame_payload), frame, sizeof(frame), &frame_length),
		WOZ_ALIRO_BLE_OK);
	T_EQ("built frame length", frame_length,
		WOZ_ALIRO_BLE_HEADER_SIZE + sizeof(frame_payload));
	T_EQ("built frame reparses", woz_aliro_ble_parse_message(frame, frame_length,
		&message, &consumed), WOZ_ALIRO_BLE_OK);
	T_EQ("built frame protocol", message.protocol, WOZ_ALIRO_BLE_PROTOCOL_UWB);
	T_OK("built frame payload", memcmp(message.payload, frame_payload,
		sizeof(frame_payload)) == 0);
	T_EQ("build rejects null payload", woz_aliro_ble_build_message(0, 0, NULL, 1, frame,
		sizeof(frame), &frame_length), WOZ_ALIRO_BLE_INVALID_ARGUMENT);
	T_EQ("build rejects reserved protocol bits", woz_aliro_ble_build_message(0xc0, 0,
		frame_payload, sizeof(frame_payload), frame, sizeof(frame), &frame_length),
		WOZ_ALIRO_BLE_INVALID_ARGUMENT);
	T_EQ("build rejects short buffer", woz_aliro_ble_build_message(0, 0, frame_payload,
		sizeof(frame_payload), frame, 5, &frame_length), WOZ_ALIRO_BLE_BUFFER_TOO_SMALL);
	T_EQ("parse rejects null data", woz_aliro_ble_parse_message(NULL, 4, &message,
		&consumed), WOZ_ALIRO_BLE_INVALID_ARGUMENT);
	T_EQ("parse rejects short header", woz_aliro_ble_parse_message(frame, 3, &message,
		&consumed), WOZ_ALIRO_BLE_TRUNCATED);
	static const uint8_t reserved_bits[] = { 0x40, 0x00, 0x00, 0x01, 0xaa };
	T_EQ("parse rejects reserved bits", woz_aliro_ble_parse_message(reserved_bits,
		sizeof(reserved_bits), &message, &consumed), WOZ_ALIRO_BLE_MALFORMED);
	T_EQ("status builder rejects null output",
		woz_aliro_ble_build_access_completed(0, 0, NULL), WOZ_ALIRO_BLE_INVALID_ARGUMENT);
	T_EQ("initiate rejects null message", woz_aliro_ble_parse_initiate_access(NULL,
		&proprietary, &proprietary_length), WOZ_ALIRO_BLE_INVALID_ARGUMENT);
	message.protocol = WOZ_ALIRO_BLE_PROTOCOL_UWB;
	message.message_id = 0;
	message.payload = frame_payload;
	message.payload_length = sizeof(frame_payload);
	T_EQ("initiate rejects wrong protocol", woz_aliro_ble_parse_initiate_access(&message,
		&proprietary, &proprietary_length), WOZ_ALIRO_BLE_MALFORMED);
	static const uint8_t wrong_attribute[] = { 0x00, 0x02, 0x80, 0x00 };
	message.protocol = WOZ_ALIRO_BLE_PROTOCOL_NOTIFICATION;
	message.message_id = WOZ_ALIRO_BLE_NOTIFICATION_INITIATE_ACCESS;
	message.payload = wrong_attribute;
	message.payload_length = sizeof(wrong_attribute);
	T_EQ("initiate rejects non-A5 attribute",
		woz_aliro_ble_parse_initiate_access(&message, &proprietary,
			&proprietary_length), WOZ_ALIRO_BLE_MALFORMED);
}
