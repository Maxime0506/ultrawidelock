#include <ultrawidelock/ultrawidelock.h>

#include <stdint.h>

int main(void)
{
	const uint8_t value[] = {0x01u, 0x00u};
	uint8_t encoded[8];
	size_t offset = 0;

	if (ultrawidelock_cred_tlv_write(encoded, sizeof(encoded), &offset, 0x5cu, value,
				sizeof(value)) != ULTRAWIDELOCK_CRED_TLV_OK) {
		return 1;
	}

	const size_t encoded_length = offset;
	struct ultrawidelock_cred_tlv parsed;

	offset = 0;
	if (ultrawidelock_cred_tlv_next(encoded, encoded_length, &offset, &parsed) !=
	    ULTRAWIDELOCK_CRED_TLV_OK) {
		return 2;
	}

	if (parsed.tag != 0x5cu || parsed.length != sizeof(value) ||
	    parsed.value[0] != value[0] || parsed.value[1] != value[1]) {
		return 3;
	}
	return 0;
}
