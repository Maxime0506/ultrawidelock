/* SPDX-License-Identifier: ISC */

// Device-side UWB ranging-service setup codec: parses the reader's M1 and M3
// setup messages, picks the device's answer to M1 (select_m2), and builds the M2
// and M4 replies. The inverse of the reader path in ultrawidelock_uwb_msg.c, written over
// the same TLV parser and builder helpers. No crypto and no session state, so a
// host loopback can drive the real reader codec end to end.
/** @file ultrawidelock_device_uwb.c — device-side UWB ranging-service setup codec.
 *  See ultrawidelock_device_uwb.h.
 */
#include "ultrawidelock_device_uwb.h"

#include <string.h>

#include "ultrawidelock_uwb_msg.h"
#include "ultrawidelock_uwb_msg_builder.h"
#include "ultrawidelock_uwb_msg_parser.h"

int ultrawidelock_dev_uwb_parse_m1(const uint8_t *msg, size_t len, struct ultrawidelock_dev_uwb_m1 *out)
{
	if (msg == NULL || len < ULTRAWIDELOCK_HEADER_LENGTH ||
	    ultrawidelock_uwb_msg_protocol_header(msg) !=
		    ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE ||
	    ultrawidelock_uwb_msg_message_id(msg) != ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M1) {
		return -1;
	}
	memset(out, 0, sizeof(*out));

	struct ultrawidelock_uwb_msg_parser p = {
		.length = len,
		.offset = ULTRAWIDELOCK_HEADER_LENGTH,
		.data = msg,
	};
	struct ultrawidelock_uwb_msg_attribute *a;
	bool have_channel = false, have_session = false;

	while ((a = ultrawidelock_uwb_msg_next_attribute(&p)) != NULL) {
		switch (a->id) {
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER:
			/* u16 array (big-endian), one entry per offered config */
			for (size_t i = 0;
			     i + 2u <= a->length && out->config_count < ULTRAWIDELOCK_DEV_UWB_MAX_CONFIGS;
			     i += 2u) {
				out->config_ids[out->config_count++] =
					(uint16_t)((uint16_t)a->value[i] << 8 | a->value[i + 1]);
			}
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO:
			for (size_t i = 0;
			     i < a->length && out->combo_count < ULTRAWIDELOCK_DEV_UWB_MAX_COMBOS; i++) {
				out->pulse_shape_combos[out->combo_count++] = a->value[i];
			}
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK:
			if (!ultrawidelock_uwb_msg_read_u8(a, "channel", &out->channel_bitmask)) {
				return -1;
			}
			have_channel = true;
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER:
			if (!ultrawidelock_uwb_msg_read_u32(a, "session", &out->session_id)) {
				return -1;
			}
			have_session = true;
			break;
		default:
			break;
		}
	}
	return (have_channel && have_session && out->config_count > 0) ? 0 : -1;
}

/**
 * Parse an M3 UWB message: extract and validate ran multiplier, chaps per slot, number of
 * responders, slots per round, sync code index bitmask, hopping configuration, and MAC mode. Return
 * 0 on success or -1 if the message is malformed or required fields are missing.
 */
int ultrawidelock_dev_uwb_parse_m3(const uint8_t *msg, size_t len, struct ultrawidelock_dev_uwb_m3 *out)
{
	if (msg == NULL || len < ULTRAWIDELOCK_HEADER_LENGTH ||
	    ultrawidelock_uwb_msg_protocol_header(msg) !=
		    ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE ||
	    ultrawidelock_uwb_msg_message_id(msg) != ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M3) {
		return -1;
	}
	memset(out, 0, sizeof(*out));

	struct ultrawidelock_uwb_msg_parser p = {
		.length = len,
		.offset = ULTRAWIDELOCK_HEADER_LENGTH,
		.data = msg,
	};
	struct ultrawidelock_uwb_msg_attribute *a;
	uint32_t seen = 0;

	while ((a = ultrawidelock_uwb_msg_next_attribute(&p)) != NULL) {
		bool ok = true;

		switch (a->id) {
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER:
			ok = ultrawidelock_uwb_msg_read_u8(a, "ran", &out->ran_multiplier);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT:
			ok = ultrawidelock_uwb_msg_read_u8(a, "chaps", &out->chaps_per_slot);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_RESPONDERS_NODES:
			ok = ultrawidelock_uwb_msg_read_u8(a, "nresp", &out->num_responders);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_SLOTS_PER_ROUND:
			ok = ultrawidelock_uwb_msg_read_u8(a, "slots", &out->slots_per_round);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK:
			ok = ultrawidelock_uwb_msg_read_u32(a, "syncmask", &out->sync_code_index_bitmask);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK:
			ok = ultrawidelock_uwb_msg_read_u8(a, "hopping", &out->hopping_config_bitmask);
			break;
		case ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_MAC_MODE:
			ok = ultrawidelock_uwb_msg_read_u8(a, "macmode", &out->mac_mode);
			break;
		default:
			continue; /* ignore unknown attributes; do not mark seen */
		}
		if (!ok) {
			return -1;
		}
		seen |= (uint32_t)1u << a->id;
	}

	/* Require the four count fields + MAC mode that drive the initiator MAC. */
	const uint32_t need = (1u << ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER) |
			      (1u << ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT) |
			      (1u << ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_RESPONDERS_NODES) |
			      (1u << ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_NUMBER_SLOTS_PER_ROUND) |
			      (1u << ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_MAC_MODE);

	return ((seen & need) == need) ? 0 : -1;
}

void ultrawidelock_dev_uwb_select_m2(const struct ultrawidelock_dev_uwb_m1 *m1, struct ultrawidelock_dev_uwb_m2_params *out)
{
	memset(out, 0, sizeof(*out));
	out->config_id = m1->config_count ? m1->config_ids[0] : 0x0001u;
	out->pulse_shape_combo = m1->combo_count ? m1->pulse_shape_combos[0] : 0x00u;
	/* M1 offers a SET of channels; M2 must name exactly ONE. Echoing the offer
	 * back is rejected outright -- a real reader answers "unsupported channel
	 * bitmask 0x03" and drops the setup with no message to the device, which is
	 * how this was found on hardware rather than here.
	 *
	 * Prefer channel 9: it is what the credential/CCC readers in this tree configure
	 * (ull_setchannel ch=9) and what the DW3110 on the bench comes up on, so
	 * picking it avoids a channel switch the reader would otherwise have to make
	 * against itself. Fall back to 5 when that is all that is offered, and to 9
	 * when a reader offers nothing legible. */
	if ((m1->channel_bitmask & ULTRAWIDELOCK_CHANNEL_BITMASK_CH5) &&
	    !(m1->channel_bitmask & ULTRAWIDELOCK_CHANNEL_BITMASK_CH9)) {
		out->channel_bitmask = ULTRAWIDELOCK_CHANNEL_BITMASK_CH5;
	} else {
		out->channel_bitmask = ULTRAWIDELOCK_CHANNEL_BITMASK_CH9;
	}
	out->ran_multiplier = 4u;
	out->slot_bitmask = 0x01u;
	out->sync_code_index_bitmask = 0x00000005u;
	out->hopping_config_bitmask = 0xFFu;
}

struct ultrawidelock_uwb_message *ultrawidelock_dev_uwb_build_m2(const struct ultrawidelock_dev_uwb_m2_params *p)
{
	struct ultrawidelock_uwb_msg_builder b;
	uint16_t plen = ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK_LENGTH +
			7 * ULTRAWIDELOCK_ATTRIBUTE_HEADER_LENGTH;

	if (!ultrawidelock_uwb_msg_builder_init(&b, plen)) {
		return NULL;
	}
	ultrawidelock_uwb_msg_builder_header(&b, ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
				     ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M2, plen);

	bool ok =
		ultrawidelock_uwb_msg_builder_add_u16(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER,
			p->config_id) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO,
			p->pulse_shape_combo) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK,
			p->channel_bitmask) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER,
			p->ran_multiplier) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK, p->slot_bitmask) &&
		ultrawidelock_uwb_msg_builder_add_u32(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK,
			p->sync_code_index_bitmask) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
			p->hopping_config_bitmask);

	if (!ok) {
		ultrawidelock_uwb_msg_free(b.message);
		return NULL;
	}
	return b.message;
}

/**
 * Build an M4 UWB message from STS index, UWB time, hop mode key, and sync code index.
 */
struct ultrawidelock_uwb_message *ultrawidelock_dev_uwb_build_m4(const struct ultrawidelock_dev_uwb_m4_params *p)
{
	struct ultrawidelock_uwb_msg_builder b;
	uint16_t plen = ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY_LENGTH +
			ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_LENGTH +
			4 * ULTRAWIDELOCK_ATTRIBUTE_HEADER_LENGTH;

	if (!ultrawidelock_uwb_msg_builder_init(&b, plen)) {
		return NULL;
	}
	ultrawidelock_uwb_msg_builder_header(&b, ULTRAWIDELOCK_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
				     ULTRAWIDELOCK_UWB_MESSAGE_SETUP_M4, plen);

	bool ok =
		ultrawidelock_uwb_msg_builder_add_u32(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_STS_INDEX0, p->sts_index0) &&
		ultrawidelock_uwb_msg_builder_add_u64(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_UWB_TIME0, p->uwb_time0) &&
		ultrawidelock_uwb_msg_builder_add_u32(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY, p->hop_mode_key) &&
		ultrawidelock_uwb_msg_builder_add_u8(
			&b, ULTRAWIDELOCK_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX,
			p->sync_code_index);

	if (!ok) {
		ultrawidelock_uwb_msg_free(b.message);
		return NULL;
	}
	return b.message;
}
