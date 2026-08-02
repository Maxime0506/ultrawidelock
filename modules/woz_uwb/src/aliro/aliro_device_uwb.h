// Device/initiator side of the UWB ranging-service setup codec: the interface for
// parsing the reader's M1 and M3 and building the device's M2 and M4. Declares the
// decoded views of M1 and M3, the parameter structs the two builders take, and
// select_m2, which chooses a config and slot layout from what M1 offered. Pure
// TLV, no crypto and no session state, so it is host-testable against the reader's
// own codec by loopback.
/** @file aliro_device_uwb.h — device/initiator side of the UWB ranging-service
 *  setup codec: the inverse of the reader's M1/M3 builders + M2/M4 handlers in
 *  aliro_uwb_msg.c. The reader builds M1/M3 and consumes M2/M4; the device (phone/
 *  fob) parses M1/M3 and builds M2/M4. Pure TLV, no crypto and no session state,
 *  so it is host-testable against the reader's own codec (loopback) and against
 *  the M2/M4 attribute sets the reader is proven to accept.
 *
 *  Copyright (c) 2026 asxeem
 *  SPDX-License-Identifier: ISC
 *  Provenance: clean-room; mirrors aliro_uwb_msg.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "aliro_uwb_msg_spec.h"

struct aliro_uwb_message; /* defined by the UWB session headers; opaque here */

#define ALIRO_DEV_UWB_MAX_CONFIGS 8
#define ALIRO_DEV_UWB_MAX_COMBOS  8

/** Ranging capabilities the reader offered in M1 (reader -> device). */
struct aliro_dev_uwb_m1 {
	uint16_t config_ids[ALIRO_DEV_UWB_MAX_CONFIGS];
	size_t config_count;
	uint8_t pulse_shape_combos[ALIRO_DEV_UWB_MAX_COMBOS];
	size_t combo_count;
	uint8_t channel_bitmask;
	uint32_t session_id;
};

/** Ranging parameters the reader committed in M3 (reader -> device). */
struct aliro_dev_uwb_m3 {
	uint8_t ran_multiplier;
	uint8_t chaps_per_slot;
	uint8_t num_responders;
	uint8_t slots_per_round;
	uint32_t sync_code_index_bitmask;
	uint8_t hopping_config_bitmask;
	uint8_t mac_mode;
};

/** Device selections carried in M2 (device -> reader). */
struct aliro_dev_uwb_m2_params {
	uint16_t config_id;
	uint8_t pulse_shape_combo;
	uint8_t channel_bitmask;
	uint8_t ran_multiplier;
	uint8_t slot_bitmask;
	uint32_t sync_code_index_bitmask;
	uint8_t hopping_config_bitmask;
};

/** Device selections carried in M4 (device -> reader). */
struct aliro_dev_uwb_m4_params {
	uint32_t sts_index0;
	uint64_t uwb_time0;
	uint32_t hop_mode_key;
	uint8_t sync_code_index;
};

/** Parse an inbound M1 / M3 message ([proto][id][len_be16][attrs]) into the
 *  structs above. Returns 0 on success, -1 on a header/attribute mismatch. */
int aliro_dev_uwb_parse_m1(const uint8_t *msg, size_t len, struct aliro_dev_uwb_m1 *out);
int aliro_dev_uwb_parse_m3(const uint8_t *msg, size_t len, struct aliro_dev_uwb_m3 *out);

/** Pick M2 selections from a parsed M1: echo the reader's first config, pulse
 *  shape and channel, and default the remaining fields to values the reader
 *  accepts. A real device would consult its own capabilities here. */
void aliro_dev_uwb_select_m2(const struct aliro_dev_uwb_m1 *m1,
			     struct aliro_dev_uwb_m2_params *out);

/** Build an M2 / M4 message. Returns a heap-allocated message (free with
 *  aliro_uwb_msg_free), or NULL on allocation/encode failure. */
struct aliro_uwb_message *aliro_dev_uwb_build_m2(const struct aliro_dev_uwb_m2_params *p);
struct aliro_uwb_message *aliro_dev_uwb_build_m4(const struct aliro_dev_uwb_m4_params *p);
