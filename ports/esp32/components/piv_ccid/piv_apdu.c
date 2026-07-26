#include "piv_apdu.h"

#include <string.h>

#define SW_SUCCESS 0x9000u
#define SW_WRONG_LENGTH 0x6700u
#define SW_FILE_NOT_FOUND 0x6a82u
#define SW_INS_NOT_SUPPORTED 0x6d00u
#define SW_CLA_NOT_SUPPORTED 0x6e00u

static const uint8_t s_piv_aid[] = {
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
};

static const uint8_t s_piv_aid_truncated[] = {
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00,
};

/*
 * NIST SP 800-73-5 Part 2, Table 3: the application property template must
 * contain the complete selected PIV AID and the coexistent tag-allocation
 * authority template. Optional label, URL, and secure-messaging algorithm
 * objects are intentionally absent.
 */
static const uint8_t s_piv_application_properties[] = {
	0x61, 0x16,
	0x4f, 0x0b,
	0xa0, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
	0x79, 0x07,
	0x4f, 0x05, 0xa0, 0x00, 0x00, 0x03, 0x08,
};

static int finish(const uint8_t *data, size_t data_len, uint16_t sw,
		  uint8_t *response, size_t response_cap, size_t *response_len)
{
	if (response == NULL || response_len == NULL ||
	    data_len > response_cap || response_cap - data_len < 2u) {
		return -1;
	}
	if (data_len != 0u) {
		memcpy(response, data, data_len);
	}
	response[data_len] = (uint8_t)(sw >> 8);
	response[data_len + 1u] = (uint8_t)sw;
	*response_len = data_len + 2u;
	return 0;
}

static bool aid_matches(const uint8_t *data, size_t len)
{
	return (len == sizeof(s_piv_aid) &&
		memcmp(data, s_piv_aid, sizeof(s_piv_aid)) == 0) ||
	       (len == sizeof(s_piv_aid_truncated) &&
		memcmp(data, s_piv_aid_truncated,
		       sizeof(s_piv_aid_truncated)) == 0);
}

int piv_apdu_transmit(bool *selected,
		      const uint8_t *command, size_t command_len,
		      uint8_t *response, size_t response_cap,
		      size_t *response_len)
{
	if (selected == NULL || command == NULL || response == NULL ||
	    response_len == NULL) {
		return -1;
	}
	*response_len = 0;
	if (command_len < 4u) {
		return finish(NULL, 0, SW_WRONG_LENGTH,
			      response, response_cap, response_len);
	}
	if (command[0] != 0x00u) {
		return finish(NULL, 0, SW_CLA_NOT_SUPPORTED,
			      response, response_cap, response_len);
	}
	if (command[1] != 0xa4u) {
		return finish(NULL, 0, SW_INS_NOT_SUPPORTED,
			      response, response_cap, response_len);
	}
	if (command[2] != 0x04u || command[3] != 0x00u ||
	    command_len < 5u) {
		return finish(NULL, 0, SW_WRONG_LENGTH,
			      response, response_cap, response_len);
	}

	size_t data_len = command[4];
	if (command_len != 5u + data_len &&
	    command_len != 6u + data_len) {
		return finish(NULL, 0, SW_WRONG_LENGTH,
			      response, response_cap, response_len);
	}
	if (!aid_matches(command + 5u, data_len)) {
		return finish(NULL, 0, SW_FILE_NOT_FOUND,
			      response, response_cap, response_len);
	}

	*selected = true;
	return finish(s_piv_application_properties,
		      sizeof(s_piv_application_properties), SW_SUCCESS,
		      response, response_cap, response_len);
}
