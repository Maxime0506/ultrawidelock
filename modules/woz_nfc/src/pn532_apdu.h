/* PN532-specific ISO 7816 APDU adaptation.
 *
 * The Aliro stack (including the prebuilt library) negotiates sizes with the
 * User Device, but has no API for the reader controller's smaller local limit.
 * This adapter keeps that hardware constraint at the transport boundary.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep every rewritten C-APDU within one PN532 host transfer chunk.  An
 * extended case-4 APDU needs seven header bytes and two Le bytes. */
#define WOZ_PN532_APDU_WIRE_MAX     240
#define WOZ_PN532_ENVELOPE_DATA_MAX (WOZ_PN532_APDU_WIRE_MAX - 9)
/* Keep the PN532 chip-to-host response comfortably inside a normal frame.
 * InDataExchange adds TFI, response-code, and status bytes around the R-APDU;
 * Le=256 can therefore force an extended PN532 frame, which is unreliable on
 * the affected modules. The transport follows SW1=61 with GET RESPONSE. */
#define WOZ_PN532_RESPONSE_DATA_MAX 240

enum woz_pn532_apdu_mode {
	WOZ_PN532_APDU_PASSTHROUGH = 0,
	WOZ_PN532_APDU_ENVELOPE,
	WOZ_PN532_APDU_GET_RESPONSE,
};

struct woz_pn532_apdu_plan {
	const uint8_t *input;
	size_t input_length;
	size_t data_offset;
	size_t data_length;
	size_t emitted_data;
	uint32_t le;
	enum woz_pn532_apdu_mode mode;
	bool extended;
	bool has_le;
	bool emitted;
	bool adapted;
};

/* Parse an APDU and prepare its transport representation. Unsupported or
 * malformed APDUs are deliberately passed through so the peer remains the
 * authority that returns an ISO 7816 status word. */
int woz_pn532_apdu_plan_init(const uint8_t *input, size_t input_length,
			     struct woz_pn532_apdu_plan *plan);

/* Emit the next wire APDU. more_internal is true only when the transport must
 * consume a 9000 response and send another fragment before notifying Aliro. */
int woz_pn532_apdu_plan_next(struct woz_pn532_apdu_plan *plan, uint8_t *output,
			     size_t output_capacity, size_t *output_length, bool *more_internal);

#ifdef __cplusplus
}
#endif
