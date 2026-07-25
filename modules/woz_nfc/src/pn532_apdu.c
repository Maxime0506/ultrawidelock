#include "pn532_apdu.h"

#include <string.h>

struct parsed_apdu {
	size_t data_offset;
	size_t data_length;
	uint32_t le;
	bool extended;
	bool has_data;
	bool has_le;
};

static bool parse_apdu(const uint8_t *input, size_t length, struct parsed_apdu *parsed)
{
	memset(parsed, 0, sizeof(*parsed));
	if (input == NULL || length < 4) {
		return false;
	}
	if (length == 4) {
		return true; /* Case 1. */
	}

	const uint8_t first_length = input[4];
	if (length == 5) { /* Case 2S. */
		parsed->has_le = true;
		parsed->le = first_length == 0 ? 256u : first_length;
		return true;
	}

	if (first_length != 0) {
		parsed->has_data = true;
		parsed->data_offset = 5;
		parsed->data_length = first_length;
		if (length == 5 + parsed->data_length) {
			return true; /* Case 3S. */
		}
		if (length == 6 + parsed->data_length) { /* Case 4S. */
			parsed->has_le = true;
			parsed->le = input[length - 1] == 0 ? 256u : input[length - 1];
			return true;
		}
		return false;
	}

	if (length < 7) {
		return false;
	}
	parsed->extended = true;
	const uint16_t value = ((uint16_t)input[5] << 8) | input[6];
	if (length == 7) { /* Case 2E. */
		parsed->has_le = true;
		parsed->le = value == 0 ? 65536u : value;
		return true;
	}
	if (value == 0) {
		return false;
	}
	parsed->has_data = true;
	parsed->data_offset = 7;
	parsed->data_length = value;
	if (length == 7 + parsed->data_length) {
		return true; /* Case 3E. */
	}
	if (length == 9 + parsed->data_length) { /* Case 4E. */
		const uint16_t le = ((uint16_t)input[length - 2] << 8) | input[length - 1];
		parsed->has_le = true;
		parsed->le = le == 0 ? 65536u : le;
		return true;
	}
	return false;
}

int woz_pn532_apdu_plan_init(const uint8_t *input, size_t input_length,
			     struct woz_pn532_apdu_plan *plan)
{
	if (input == NULL || input_length == 0 || plan == NULL) {
		return -1;
	}
	memset(plan, 0, sizeof(*plan));
	plan->input = input;
	plan->input_length = input_length;

	struct parsed_apdu parsed;
	if (!parse_apdu(input, input_length, &parsed)) {
		plan->mode = WOZ_PN532_APDU_PASSTHROUGH;
		return 0;
	}

	if (input[1] == 0xc3 && parsed.has_data) {
		plan->mode = WOZ_PN532_APDU_ENVELOPE;
		plan->data_offset = parsed.data_offset;
		plan->data_length = parsed.data_length;
		plan->extended = parsed.extended;
		plan->has_le = parsed.has_le;
		plan->le = parsed.le;
		plan->adapted = parsed.data_length > WOZ_PN532_ENVELOPE_DATA_MAX ||
				(parsed.has_le && parsed.le > WOZ_PN532_RESPONSE_DATA_MAX);
	} else if (input[1] == 0xc0 && !parsed.has_data && parsed.has_le) {
		plan->mode = WOZ_PN532_APDU_GET_RESPONSE;
		plan->extended = parsed.extended;
		plan->has_le = true;
		plan->le = parsed.le;
		plan->adapted = parsed.le > WOZ_PN532_RESPONSE_DATA_MAX;
	}
	return 0;
}

static int emit_passthrough(struct woz_pn532_apdu_plan *plan, uint8_t *output,
			    size_t output_capacity, size_t *output_length)
{
	if (plan->emitted || plan->input_length > output_capacity) {
		return -2;
	}
	memcpy(output, plan->input, plan->input_length);
	*output_length = plan->input_length;
	plan->emitted = true;
	return 0;
}

static int emit_get_response(struct woz_pn532_apdu_plan *plan, uint8_t *output,
			     size_t output_capacity, size_t *output_length)
{
	if (plan->emitted || plan->input_length > output_capacity) {
		return -2;
	}
	memcpy(output, plan->input, plan->input_length);
	const uint32_t le =
		plan->le > WOZ_PN532_RESPONSE_DATA_MAX ? WOZ_PN532_RESPONSE_DATA_MAX : plan->le;
	if (plan->extended) {
		output[5] = (uint8_t)(le >> 8);
		output[6] = (uint8_t)le;
	} else {
		output[4] = (uint8_t)le;
	}
	*output_length = plan->input_length;
	plan->emitted = true;
	return 0;
}

static int emit_envelope(struct woz_pn532_apdu_plan *plan, uint8_t *output, size_t output_capacity,
			 size_t *output_length, bool *more_internal)
{
	if (plan->emitted_data >= plan->data_length) {
		return -2;
	}
	size_t fragment = plan->data_length - plan->emitted_data;
	if (fragment > WOZ_PN532_ENVELOPE_DATA_MAX) {
		fragment = WOZ_PN532_ENVELOPE_DATA_MAX;
	}
	const bool more = plan->emitted_data + fragment < plan->data_length;
	const bool include_le = !more && plan->has_le;
	const size_t header_length = plan->extended ? 7 : 5;
	const size_t needed =
		header_length + fragment + (include_le ? (plan->extended ? 2 : 1) : 0);
	if (needed > output_capacity || needed > WOZ_PN532_APDU_WIRE_MAX) {
		return -2;
	}

	output[0] = more ? (uint8_t)(plan->input[0] | 0x10u) : plan->input[0];
	output[1] = plan->input[1];
	output[2] = plan->input[2];
	output[3] = plan->input[3];
	size_t at = 4;
	if (plan->extended) {
		output[at++] = 0;
		output[at++] = (uint8_t)(fragment >> 8);
		output[at++] = (uint8_t)fragment;
	} else {
		output[at++] = (uint8_t)fragment;
	}
	memcpy(output + at, plan->input + plan->data_offset + plan->emitted_data, fragment);
	at += fragment;
	if (include_le) {
		const uint32_t le = plan->le > WOZ_PN532_RESPONSE_DATA_MAX
					    ? WOZ_PN532_RESPONSE_DATA_MAX
					    : plan->le;
		if (plan->extended) {
			output[at++] = (uint8_t)(le >> 8);
			output[at++] = (uint8_t)le;
		} else {
			output[at++] = (uint8_t)le;
		}
	}

	plan->emitted_data += fragment;
	*output_length = at;
	*more_internal = more;
	return 0;
}

int woz_pn532_apdu_plan_next(struct woz_pn532_apdu_plan *plan, uint8_t *output,
			     size_t output_capacity, size_t *output_length, bool *more_internal)
{
	if (plan == NULL || output == NULL || output_length == NULL || more_internal == NULL) {
		return -1;
	}
	*output_length = 0;
	*more_internal = false;
	switch (plan->mode) {
	case WOZ_PN532_APDU_ENVELOPE:
		return emit_envelope(plan, output, output_capacity, output_length, more_internal);
	case WOZ_PN532_APDU_GET_RESPONSE:
		return emit_get_response(plan, output, output_capacity, output_length);
	case WOZ_PN532_APDU_PASSTHROUGH:
	default:
		return emit_passthrough(plan, output, output_capacity, output_length);
	}
}
