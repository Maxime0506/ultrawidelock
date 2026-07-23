// libFuzzer entry for the presence-assertion verifier. Feeds arbitrary bytes as a
// wire frame: aliro_assert_verify must never read out of bounds or crash on a
// short/oversized/garbage frame — every malformed input returns a clean negative
// verdict. Built under ASan/UBSan (see fuzz.sh).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stddef.h>
#include <stdint.h>

#include "aliro_assert.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* Fixed key + nonce: the fuzzer explores framing/length/field paths, not the
	 * MAC (a random frame practically never authenticates, exercising the reject
	 * paths; the happy path is covered by the KAT suite). */
	static const uint8_t key[ALIRO_ASSERT_KEY_LEN] = { 0 };
	static const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN] = { 0 };
	struct aliro_assert out;

	(void)aliro_assert_verify(key, data, size, nonce, 40, 0, &out);
	return 0;
}
