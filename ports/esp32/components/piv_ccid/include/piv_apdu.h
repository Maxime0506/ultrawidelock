#ifndef WOZ_PIV_APDU_H
#define WOZ_PIV_APDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIV_APDU_MAX_RESPONSE 1024u

/*
 * Handle one short ISO 7816 command APDU.
 *
 * The initial interoperability slice implements SELECT for the full or
 * right-truncated NIST PIV application identifier. Unsupported commands fail
 * with a status word. The function never returns a success response without a
 * complete status word.
 */
int piv_apdu_transmit(bool *selected,
		      const uint8_t *command, size_t command_len,
		      uint8_t *response, size_t response_cap,
		      size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif
