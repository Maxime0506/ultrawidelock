// libFuzzer entry for the Aliro step-up DeviceResponse parser. Drives the crypto-free CBOR decoder
// (aliro_stepup_parse.c) with arbitrary bytes: it must never read out of bounds, recurse without
// bound, or crash — malformed input returns a clean error. Built under ASan/UBSan (see fuzz.sh).
#include <stddef.h>
#include <stdint.h>

#include "aliro_stepup.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct aliro_stepup_doc doc;

	(void)aliro_stepup_parse_response(data, size, &doc);
	return 0;
}
