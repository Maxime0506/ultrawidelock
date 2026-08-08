/* CBMC harness — aliro_uwb_msg TLV parser memory safety.
 *
 * Proves that walking every attribute of an arbitrary message up to MSG_MAX
 * bytes, and decoding each attribute at all four widths, never reads or writes
 * out of bounds — for ALL inputs in that range, not just sampled ones. The fix
 * at aliro_uwb_msg_parser.c:24 (the lone-trailing-byte over-read) is what makes
 * it pass. */
#include <stddef.h>
#include <stdint.h>

#include "aliro_uwb_msg_parser.h"
#include "aliro_uwb_msg_spec.h"

/* Small bound: attributes advance the cursor by >= 2 bytes, so a handful of
 * bytes already covers multi-attribute payloads, overruns, and odd trailers. */
#define MSG_MAX 12

size_t nondet_size(void);

void harness(void)
{
	uint8_t buf[MSG_MAX];
	size_t size = nondet_size();

	__CPROVER_assume(size >= ALIRO_HEADER_LENGTH && size <= MSG_MAX);

	struct aliro_uwb_msg_parser parser = {
		.length = size,
		.offset = ALIRO_HEADER_LENGTH,
		.data = buf,
	};
	struct aliro_uwb_msg_attribute *attr;

	while ((attr = aliro_uwb_msg_next_attribute(&parser)) != NULL) {
		uint8_t v8;
		uint16_t v16;
		uint32_t v32;
		uint64_t v64;

		(void)aliro_uwb_msg_read_u8(attr, "", &v8);
		(void)aliro_uwb_msg_read_u16(attr, "", &v16);
		(void)aliro_uwb_msg_read_u32(attr, "", &v32);
		(void)aliro_uwb_msg_read_u64(attr, "", &v64);
	}
}
