// Device-side UWB ranging-service setup codec: parses the reader's M1 and M3
// setup messages, picks the device's answer to M1 (select_m2), and builds the M2
// and M4 replies. The inverse of the reader path in aliro_uwb_msg.c, written over
// the same TLV parser and builder helpers. No crypto and no session state, so a
// host loopback can drive the real reader codec end to end.
/** @file aliro_device_uwb.c — device-side UWB ranging-service setup codec.
 *  See aliro_device_uwb.h.
 *
 *  Copyright (c) 2026 asxeem
 *  SPDX-License-Identifier: ISC
 */
#include "aliro_device_uwb.h"

#include <string.h>

#include "aliro_uwb_msg.h"
#include "aliro_uwb_msg_builder.h"
#include "aliro_uwb_msg_parser.h"

int aliro_dev_uwb_parse_m1(const uint8_t *msg, size_t len, struct aliro_dev_uwb_m1 *out)
{
	if (msg == NULL || len < ALIRO_HEADER_LENGTH ||
	    aliro_uwb_msg_protocol_header(msg) != ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE ||
	    aliro_uwb_msg_message_id(msg) != ALIRO_UWB_MESSAGE_SETUP_M1) {
		return -1;
	}
	memset(out, 0, sizeof(*out));

	struct aliro_uwb_msg_parser p = {
		.length = len,
		.offset = ALIRO_HEADER_LENGTH,
		.data = msg,
	};
	struct aliro_uwb_msg_attribute *a;
	bool have_channel = false, have_session = false;

	while ((a = aliro_uwb_msg_next_attribute(&p)) != NULL) {
		switch (a->id) {
		case ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER:
			/* u16 array (big-endian), one entry per offered config */
			for (size_t i = 0;
			     i + 2u <= a->length && out->config_count < ALIRO_DEV_UWB_MAX_CONFIGS;
			     i += 2u) {
				out->config_ids[out->config_count++] =
					(uint16_t)((uint16_t)a->value[i] << 8 | a->value[i + 1]);
			}
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO:
			for (size_t i = 0;
			     i < a->length && out->combo_count < ALIRO_DEV_UWB_MAX_COMBOS; i++) {
				out->pulse_shape_combos[out->combo_count++] = a->value[i];
			}
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK:
			if (!aliro_uwb_msg_read_u8(a, "channel", &out->channel_bitmask)) {
				return -1;
			}
			have_channel = true;
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_SESSION_IDENTIFIER:
			if (!aliro_uwb_msg_read_u32(a, "session", &out->session_id)) {
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

int aliro_dev_uwb_parse_m3(const uint8_t *msg, size_t len, struct aliro_dev_uwb_m3 *out)
{
	if (msg == NULL || len < ALIRO_HEADER_LENGTH ||
	    aliro_uwb_msg_protocol_header(msg) != ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE ||
	    aliro_uwb_msg_message_id(msg) != ALIRO_UWB_MESSAGE_SETUP_M3) {
		return -1;
	}
	memset(out, 0, sizeof(*out));

	struct aliro_uwb_msg_parser p = {
		.length = len,
		.offset = ALIRO_HEADER_LENGTH,
		.data = msg,
	};
	struct aliro_uwb_msg_attribute *a;
	uint32_t seen = 0;

	while ((a = aliro_uwb_msg_next_attribute(&p)) != NULL) {
		bool ok = true;

		switch (a->id) {
		case ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER:
			ok = aliro_uwb_msg_read_u8(a, "ran", &out->ran_multiplier);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT:
			ok = aliro_uwb_msg_read_u8(a, "chaps", &out->chaps_per_slot);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_RESPONDERS_NODES:
			ok = aliro_uwb_msg_read_u8(a, "nresp", &out->num_responders);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_SLOTS_PER_ROUND:
			ok = aliro_uwb_msg_read_u8(a, "slots", &out->slots_per_round);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK:
			ok = aliro_uwb_msg_read_u32(a, "syncmask", &out->sync_code_index_bitmask);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK:
			ok = aliro_uwb_msg_read_u8(a, "hopping", &out->hopping_config_bitmask);
			break;
		case ALIRO_UWB_RANGING_SERVICE_ATTR_MAC_MODE:
			ok = aliro_uwb_msg_read_u8(a, "macmode", &out->mac_mode);
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
	const uint32_t need = (1u << ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER) |
			      (1u << ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_CHAPS_PER_SLOT) |
			      (1u << ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_RESPONDERS_NODES) |
			      (1u << ALIRO_UWB_RANGING_SERVICE_ATTR_NUMBER_SLOTS_PER_ROUND) |
			      (1u << ALIRO_UWB_RANGING_SERVICE_ATTR_MAC_MODE);

	return ((seen & need) == need) ? 0 : -1;
}

void aliro_dev_uwb_select_m2(const struct aliro_dev_uwb_m1 *m1, struct aliro_dev_uwb_m2_params *out)
{
	memset(out, 0, sizeof(*out));
	out->config_id = m1->config_count ? m1->config_ids[0] : 0x0001u;
	out->pulse_shape_combo = m1->combo_count ? m1->pulse_shape_combos[0] : 0x00u;
	out->channel_bitmask = m1->channel_bitmask ? m1->channel_bitmask : 0x01u;
	out->ran_multiplier = 4u;
	out->slot_bitmask = 0x01u;
	out->sync_code_index_bitmask = 0x00000005u;
	out->hopping_config_bitmask = 0xFFu;
}

struct aliro_uwb_message *aliro_dev_uwb_build_m2(const struct aliro_dev_uwb_m2_params *p)
{
	struct aliro_uwb_msg_builder b;
	uint16_t plen = ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK_LENGTH +
			7 * ALIRO_ATTRIBUTE_HEADER_LENGTH;

	if (!aliro_uwb_msg_builder_init(&b, plen)) {
		return NULL;
	}
	aliro_uwb_msg_builder_header(&b, ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
				     ALIRO_UWB_MESSAGE_SETUP_M2, plen);

	bool ok = aliro_uwb_msg_builder_add_u16(
			  &b, ALIRO_UWB_RANGING_SERVICE_ATTR_CONFIGURATION_IDENTIFIER,
			  p->config_id) &&
		  aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_PULSE_SHAPE_COMBO,
					       p->pulse_shape_combo) &&
		  aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_CHANNEL_BITMASK,
					       p->channel_bitmask) &&
		  aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_RAN_MULTIPLIER,
					       p->ran_multiplier) &&
		  aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_SLOT_BITMASK,
					       p->slot_bitmask) &&
		  aliro_uwb_msg_builder_add_u32(
			  &b, ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_BITMASK,
			  p->sync_code_index_bitmask) &&
		  aliro_uwb_msg_builder_add_u8(
			  &b, ALIRO_UWB_RANGING_SERVICE_ATTR_HOPPING_CONFIGURATION_BITMASK,
			  p->hopping_config_bitmask);

	if (!ok) {
		aliro_uwb_msg_free(b.message);
		return NULL;
	}
	return b.message;
}

struct aliro_uwb_message *aliro_dev_uwb_build_m4(const struct aliro_dev_uwb_m4_params *p)
{
	struct aliro_uwb_msg_builder b;
	uint16_t plen = ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_UWB_TIME0_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY_LENGTH +
			ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX_LENGTH +
			4 * ALIRO_ATTRIBUTE_HEADER_LENGTH;

	if (!aliro_uwb_msg_builder_init(&b, plen)) {
		return NULL;
	}
	aliro_uwb_msg_builder_header(&b, ALIRO_UWB_PROTOCOL_TYPE_UWB_RANGING_SERVICE,
				     ALIRO_UWB_MESSAGE_SETUP_M4, plen);

	bool ok = aliro_uwb_msg_builder_add_u32(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_STS_INDEX0,
						p->sts_index0) &&
		  aliro_uwb_msg_builder_add_u64(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_UWB_TIME0,
						p->uwb_time0) &&
		  aliro_uwb_msg_builder_add_u32(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_HOP_MODE_KEY,
						p->hop_mode_key) &&
		  aliro_uwb_msg_builder_add_u8(&b, ALIRO_UWB_RANGING_SERVICE_ATTR_SYNC_CODE_INDEX,
					       p->sync_code_index);

	if (!ok) {
		aliro_uwb_msg_free(b.message);
		return NULL;
	}
	return b.message;
}
