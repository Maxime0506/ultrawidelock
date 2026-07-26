#include "piv_apdu.h"
#include "piv_ccid.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, label) do {                                                \
	if (cond) {                                                              \
		printf("  ok  %s\n", label);                                      \
	} else {                                                                 \
		printf("  FAIL %s\n", label);                                     \
		failures++;                                                          \
	}                                                                        \
} while (0)

static size_t request(uint8_t *out, uint8_t type, uint32_t payload_len,
		      uint8_t slot, uint8_t seq, uint8_t p0,
		      const uint8_t *payload)
{
	out[0] = type;
	out[1] = (uint8_t)payload_len;
	out[2] = (uint8_t)(payload_len >> 8);
	out[3] = (uint8_t)(payload_len >> 16);
	out[4] = (uint8_t)(payload_len >> 24);
	out[5] = slot;
	out[6] = seq;
	out[7] = p0;
	out[8] = 0;
	out[9] = 0;
	if (payload_len != 0 && payload != NULL) {
		memcpy(out + PIV_CCID_HEADER_LEN, payload, payload_len);
	}
	return PIV_CCID_HEADER_LEN + payload_len;
}

static uint32_t response_payload_len(const uint8_t *response)
{
	return (uint32_t)response[1] |
	       ((uint32_t)response[2] << 8) |
	       ((uint32_t)response[3] << 16) |
	       ((uint32_t)response[4] << 24);
}

static void test_slot_and_power(void)
{
	struct piv_ccid ccid;
	uint8_t req[128];
	uint8_t rsp[128];
	size_t req_len;
	size_t rsp_len = 0;

	piv_ccid_init(&ccid);
	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  0, 0, 7, 0, NULL);
	CHECK(piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp),
			       &rsp_len) == 0,
	      "slot-status request is framed");
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_SLOT_STATUS &&
	      rsp[6] == 7 && rsp[7] == 1 && rsp_len == PIV_CCID_HEADER_LEN,
	      "slot starts present and inactive, sequence echoed");

	req_len = request(req, PIV_CCID_PC_TO_RDR_ICC_POWER_ON,
			  0, 0, 8, 0, NULL);
	CHECK(piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp),
			       &rsp_len) == 0,
	      "power-on request succeeds");
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_DATA_BLOCK &&
	      rsp[7] == 0 && response_payload_len(rsp) == 4 &&
	      memcmp(rsp + PIV_CCID_HEADER_LEN,
		     (uint8_t[]){0x3b, 0x80, 0x01, 0x81}, 4) == 0,
	      "power-on returns a valid minimal T=1 ATR");

	req_len = request(req, PIV_CCID_PC_TO_RDR_ICC_POWER_OFF,
			  0, 0, 9, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_SLOT_STATUS && rsp[7] == 1,
	      "power-off returns the card to inactive");
}

static void test_piv_select(void)
{
	static const uint8_t select_piv[] = {
		0x00, 0xa4, 0x04, 0x00, 0x09,
		0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00,
		0x00,
	};
	struct piv_ccid ccid;
	uint8_t req[128];
	uint8_t rsp[128];
	size_t req_len;
	size_t rsp_len = 0;

	piv_ccid_init(&ccid);
	req_len = request(req, PIV_CCID_PC_TO_RDR_ICC_POWER_ON,
			  0, 0, 1, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);

	req_len = request(req, PIV_CCID_PC_TO_RDR_XFR_BLOCK,
			  sizeof(select_piv), 0, 2, 0, select_piv);
	CHECK(piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp),
			       &rsp_len) == 0,
	      "PIV SELECT travels through CCID XfrBlock");
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_DATA_BLOCK && rsp[6] == 2 &&
	      rsp[rsp_len - 2] == 0x90 && rsp[rsp_len - 1] == 0x00 &&
	      rsp[PIV_CCID_HEADER_LEN] == 0x61 && ccid.piv_selected,
	      "right-truncated PIV AID returns an application template");

	uint8_t wrong[sizeof(select_piv)];
	memcpy(wrong, select_piv, sizeof(wrong));
	wrong[5] ^= 0x01;
	req_len = request(req, PIV_CCID_PC_TO_RDR_XFR_BLOCK,
			  sizeof(wrong), 0, 3, 0, wrong);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK(rsp[rsp_len - 2] == 0x6a && rsp[rsp_len - 1] == 0x82,
	      "unknown application AID fails closed");
}

static void test_rejections_and_parameters(void)
{
	struct piv_ccid ccid;
	uint8_t req[128];
	uint8_t rsp[128];
	size_t req_len;
	size_t rsp_len = 0;

	piv_ccid_init(&ccid);
	CHECK(piv_ccid_process(&ccid, req, 9, rsp, sizeof(rsp),
			       &rsp_len) == -1,
	      "truncated CCID header produces no guessed response");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  0, 1, 4, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0x05,
	      "nonzero slot is rejected");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_SLOT_STATUS,
			  1, 0, 5, 0, NULL);
	piv_ccid_process(&ccid, req, PIV_CCID_HEADER_LEN,
			 rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0x01,
	      "declared payload length mismatch is rejected");

	req_len = request(req, PIV_CCID_PC_TO_RDR_GET_PARAMETERS,
			  0, 0, 6, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK(rsp[0] == PIV_CCID_RDR_TO_PC_PARAMETERS &&
	      rsp[9] == 1 && response_payload_len(rsp) == 7,
	      "reader reports bounded T=1 parameters");

	req_len = request(req, 0xff, 0, 0, 7, 0, NULL);
	piv_ccid_process(&ccid, req, req_len, rsp, sizeof(rsp), &rsp_len);
	CHECK((rsp[7] & 0x40) != 0 && rsp[8] == 0,
	      "unsupported CCID command fails closed");
}

int main(void)
{
	printf("-- PIV CCID protocol core --\n");
	CHECK(PIV_CCID_FUNCTIONAL_DESCRIPTOR_TYPE == 0x21u,
	      "CCID functional descriptor uses USB-IF type 0x21");
	test_slot_and_power();
	test_piv_select();
	test_rejections_and_parameters();
	printf("RESULT %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
