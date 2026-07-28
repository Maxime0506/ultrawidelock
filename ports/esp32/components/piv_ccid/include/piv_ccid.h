#ifndef WOZ_PIV_CCID_H
#define WOZ_PIV_CCID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "piv_apdu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIV_CCID_HEADER_LEN 10u
#define PIV_CCID_MAX_MESSAGE 1034u
#define PIV_CCID_FUNCTIONAL_DESCRIPTOR_TYPE 0x21u

enum piv_ccid_message {
	PIV_CCID_PC_TO_RDR_SET_PARAMETERS = 0x61,
	PIV_CCID_PC_TO_RDR_ICC_POWER_ON = 0x62,
	PIV_CCID_PC_TO_RDR_ICC_POWER_OFF = 0x63,
	PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS = 0x65,
	PIV_CCID_PC_TO_RDR_SECURE = 0x69,
	PIV_CCID_PC_TO_RDR_T0_APDU = 0x6a,
	PIV_CCID_PC_TO_RDR_ESCAPE = 0x6b,
	PIV_CCID_PC_TO_RDR_GET_PARAMETERS = 0x6c,
	PIV_CCID_PC_TO_RDR_RESET_PARAMETERS = 0x6d,
	PIV_CCID_PC_TO_RDR_ICC_CLOCK = 0x6e,
	PIV_CCID_PC_TO_RDR_XFR_BLOCK = 0x6f,
	PIV_CCID_PC_TO_RDR_MECHANICAL = 0x71,
	PIV_CCID_PC_TO_RDR_ABORT = 0x72,
	PIV_CCID_PC_TO_RDR_SET_DATA_RATE_AND_CLOCK = 0x73,

	PIV_CCID_RDR_TO_PC_DATA_BLOCK = 0x80,
	PIV_CCID_RDR_TO_PC_SLOT_STATUS = 0x81,
	PIV_CCID_RDR_TO_PC_PARAMETERS = 0x82,
	PIV_CCID_RDR_TO_PC_ESCAPE = 0x83,
	PIV_CCID_RDR_TO_PC_DATA_RATE_AND_CLOCK = 0x84,
};

struct piv_ccid {
	bool powered;
	struct piv_apdu piv;
};

void piv_ccid_init(struct piv_ccid *ccid,
		   const struct piv_apdu_backend *backend, void *backend_ctx,
		   bool pin_required);

/*
 * Process one complete CCID bulk-OUT message.
 *
 * Returns 0 after producing a CCID response, including protocol-level command
 * failures. Returns -1 only when no safely framed response can be produced
 * (null pointers, a truncated CCID header, or an undersized output buffer).
 */
int piv_ccid_process(struct piv_ccid *ccid,
		     const uint8_t *request, size_t request_len,
		     uint8_t *response, size_t response_cap,
		     size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif
