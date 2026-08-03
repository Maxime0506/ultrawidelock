#include "piv_ccid.h"

#include "piv_apdu.h"

#include <string.h>

#define CCID_STATUS_COMMAND_FAILED 0x40u
#define CCID_STATUS_ICC_ACTIVE 0x00u
#define CCID_STATUS_ICC_INACTIVE 0x01u
#define CCID_STATUS_ICC_ABSENT 0x02u

#define CCID_ERROR_CMD_NOT_SUPPORTED 0x00u
#define CCID_ERROR_BAD_LENGTH 0x01u
#define CCID_ERROR_SLOT_NOT_FOUND 0x05u
#define CCID_ERROR_ICC_MUTE 0xfeu

#define CCID_PROTOCOL_T1 0x01u

static const uint8_t s_atr[] = {0x3b, 0x80, 0x01, 0x81};

/* CCID 1.1 T=1 parameter structure. */
static const uint8_t s_t1_parameters[] = {
	0x11, /* Fi/Di = 372/1 */
	0x10, /* inverse convention off, CRC off */
	0x00, /* extra guard time */
	0x4d, /* BWI=4, CWI=13 */
	0x00, /* clock-stop not supported */
	0xfe, /* information field size */
	0x00, /* NAD */
};

/**
 * Read a little-endian 32-bit unsigned integer from a 4-byte buffer.
 */
static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

/**
 * Write a little-endian 32-bit unsigned integer into a 4-byte buffer.
 */
static void put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

/**
 * Encode card status byte reflecting current power state: CCID_STATUS_ICC_ACTIVE if powered, else
 * CCID_STATUS_ICC_INACTIVE.
 */
static uint8_t icc_status(const struct piv_ccid *ccid)
{
	return ccid->powered ? CCID_STATUS_ICC_ACTIVE :
			       CCID_STATUS_ICC_INACTIVE;
}

static int reply(uint8_t type, uint8_t slot, uint8_t seq,
		 uint8_t status, uint8_t error, uint8_t parameter,
		 const uint8_t *payload, size_t payload_len,
		 uint8_t *response, size_t response_cap,
		 size_t *response_len)
{
	if (response == NULL || response_len == NULL ||
	    payload_len > UINT32_MAX ||
	    response_cap < PIV_CCID_HEADER_LEN ||
	    payload_len > response_cap - PIV_CCID_HEADER_LEN) {
		return -1;
	}

	response[0] = type;
	put_le32(response + 1u, (uint32_t)payload_len);
	response[5] = slot;
	response[6] = seq;
	response[7] = status;
	response[8] = error;
	response[9] = parameter;
	if (payload_len != 0u) {
		memcpy(response + PIV_CCID_HEADER_LEN, payload, payload_len);
	}
	*response_len = PIV_CCID_HEADER_LEN + payload_len;
	return 0;
}

static int slot_status(const struct piv_ccid *ccid, uint8_t slot, uint8_t seq,
		       uint8_t command_status, uint8_t error,
		       uint8_t *response, size_t response_cap,
		       size_t *response_len)
{
	uint8_t card_status = slot == 0u ? icc_status(ccid) :
					  CCID_STATUS_ICC_ABSENT;
	return reply(PIV_CCID_RDR_TO_PC_SLOT_STATUS, slot, seq,
		     command_status | card_status, error, 0x00,
		     NULL, 0, response, response_cap, response_len);
}

/**
 * Initialize PIV CCID protocol handler: clear struct, register APDU backend and context, set PIN
 * requirement flag.
 */
void piv_ccid_init(struct piv_ccid *ccid,
		   const struct piv_apdu_backend *backend, void *backend_ctx,
		   bool pin_required)
{
	if (ccid != NULL) {
		memset(ccid, 0, sizeof(*ccid));
		piv_apdu_init(&ccid->piv, backend, backend_ctx, pin_required);
	}
}

int piv_ccid_process(struct piv_ccid *ccid,
		     const uint8_t *request, size_t request_len,
		     uint8_t *response, size_t response_cap,
		     size_t *response_len)
{
	if (ccid == NULL || request == NULL || response == NULL ||
	    response_len == NULL || request_len < PIV_CCID_HEADER_LEN) {
		return -1;
	}
	*response_len = 0;

	uint8_t type = request[0];
	uint32_t payload_len = get_le32(request + 1u);
	uint8_t slot = request[5];
	uint8_t seq = request[6];

	if (slot != 0u) {
		return slot_status(ccid, slot, seq, CCID_STATUS_COMMAND_FAILED,
				   CCID_ERROR_SLOT_NOT_FOUND,
				   response, response_cap, response_len);
	}
	if (payload_len > PIV_CCID_MAX_MESSAGE - PIV_CCID_HEADER_LEN ||
	    payload_len != request_len - PIV_CCID_HEADER_LEN) {
		return slot_status(ccid, slot, seq, CCID_STATUS_COMMAND_FAILED,
				   CCID_ERROR_BAD_LENGTH,
				   response, response_cap, response_len);
	}

	switch (type) {
	case PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS:
	case PIV_CCID_PC_TO_RDR_ABORT:
		return slot_status(ccid, slot, seq, 0, 0,
				   response, response_cap, response_len);

	case PIV_CCID_PC_TO_RDR_ICC_POWER_ON:
		if (payload_len != 0u) {
			return slot_status(ccid, slot, seq,
					   CCID_STATUS_COMMAND_FAILED,
					   CCID_ERROR_BAD_LENGTH,
					   response, response_cap, response_len);
		}
		ccid->powered = true;
		piv_apdu_reset(&ccid->piv);
		return reply(PIV_CCID_RDR_TO_PC_DATA_BLOCK, slot, seq,
			     CCID_STATUS_ICC_ACTIVE, 0, 0,
			     s_atr, sizeof(s_atr),
			     response, response_cap, response_len);

	case PIV_CCID_PC_TO_RDR_ICC_POWER_OFF:
		if (payload_len != 0u) {
			return slot_status(ccid, slot, seq,
					   CCID_STATUS_COMMAND_FAILED,
					   CCID_ERROR_BAD_LENGTH,
					   response, response_cap, response_len);
		}
		ccid->powered = false;
		piv_apdu_reset(&ccid->piv);
		return slot_status(ccid, slot, seq, 0, 0,
				   response, response_cap, response_len);

	case PIV_CCID_PC_TO_RDR_GET_PARAMETERS:
	case PIV_CCID_PC_TO_RDR_RESET_PARAMETERS:
	case PIV_CCID_PC_TO_RDR_SET_PARAMETERS:
		if (type == PIV_CCID_PC_TO_RDR_SET_PARAMETERS &&
		    (request[7] != CCID_PROTOCOL_T1 ||
		     payload_len != sizeof(s_t1_parameters))) {
			return slot_status(ccid, slot, seq,
					   CCID_STATUS_COMMAND_FAILED,
					   CCID_ERROR_BAD_LENGTH,
					   response, response_cap, response_len);
		}
		return reply(PIV_CCID_RDR_TO_PC_PARAMETERS, slot, seq,
			     icc_status(ccid), 0, CCID_PROTOCOL_T1,
			     s_t1_parameters, sizeof(s_t1_parameters),
			     response, response_cap, response_len);

	case PIV_CCID_PC_TO_RDR_XFR_BLOCK: {
		uint8_t apdu_response[PIV_APDU_MAX_RESPONSE];
		size_t apdu_response_len = 0;

		if (!ccid->powered) {
			return slot_status(ccid, slot, seq,
					   CCID_STATUS_COMMAND_FAILED,
					   CCID_ERROR_ICC_MUTE,
					   response, response_cap, response_len);
		}
		if (piv_apdu_transmit(&ccid->piv,
				      request + PIV_CCID_HEADER_LEN, payload_len,
				      apdu_response, sizeof(apdu_response),
				      &apdu_response_len) != 0) {
			return slot_status(ccid, slot, seq,
					   CCID_STATUS_COMMAND_FAILED,
					   CCID_ERROR_BAD_LENGTH,
					   response, response_cap, response_len);
		}
		return reply(PIV_CCID_RDR_TO_PC_DATA_BLOCK, slot, seq,
			     CCID_STATUS_ICC_ACTIVE, 0, 0,
			     apdu_response, apdu_response_len,
			     response, response_cap, response_len);
	}

	default:
		return slot_status(ccid, slot, seq, CCID_STATUS_COMMAND_FAILED,
				   CCID_ERROR_CMD_NOT_SUPPORTED,
				   response, response_cap, response_len);
	}
}
