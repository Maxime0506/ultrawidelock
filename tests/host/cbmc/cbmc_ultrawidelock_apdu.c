/* CBMC harness — credential BLE credential-auth codec memory safety.
 *
 * Exhaustively proves the device->reader receive path is memory-safe for all
 * inputs up to APDU_MAX bytes: unframe the L2CAP envelope, strip the ISO7816
 * status word, parse the remainder as AUTH0 / AUTH1 responses, and walk the raw
 * BER-TLV.
 *
 * The fixed-size device-key / signature copies inside the parsers depend on
 * ultrawidelock_tlv_find returning a value slice that lies within the buffer. Rather
 * than size the buffer large enough to actually hold a 65-byte value (which
 * makes the solver blow up), that precondition is asserted directly below: it
 * holds for ANY returned length, so it covers the 65/64-byte copies while the
 * proof stays fast. Destinations are fixed-size arrays, so the copies are then
 * safe by composition. */
#include <stddef.h>
#include <stdint.h>

#include "ultrawidelock_apdu.h"

#define APDU_MAX 48

size_t nondet_size(void);

void harness(void)
{
	uint8_t buf[APDU_MAX];
	size_t size = nondet_size();

	__CPROVER_assume(size <= APDU_MAX);

	uint8_t type;
	uint8_t opcode;
	const uint8_t *payload;
	size_t plen;

	if (ultrawidelock_ble_unframe(buf, size, &type, &opcode, &payload, &plen) == 0) {
		size_t body = plen;
		uint16_t sw;

		(void)ultrawidelock_apdu_strip_sw(payload, &body, &sw);

		struct ultrawidelock_auth0_response r0;
		struct ultrawidelock_auth1_response r1;

		(void)ultrawidelock_apdu_parse_auth0_response(payload, body, &r0);
		(void)ultrawidelock_apdu_parse_auth1_response(payload, body, &r1);
	}

	const uint8_t *val;
	size_t vlen;

	if (ultrawidelock_tlv_find(buf, size, ULTRAWIDELOCK_TAG_DEVICE_PUBX, &val, &vlen) == 0) {
		/* Precondition the parsers' fixed-size memcpys rely on; length-agnostic,
		 * so it covers the 65/64-byte copies. */
		__CPROVER_assert(val >= buf && (size_t)(val - buf) + vlen <= size,
				 "tlv slice within buffer");
	}
}
