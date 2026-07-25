// libFuzzer entry for the presence-assertion verifier. Feeds arbitrary bytes as a
// wire frame: aliro_assert_verify_p256 must never read out of bounds or crash on
// a short/oversized/garbage frame — every malformed input returns a clean
// negative verdict. Built under ASan/UBSan (see fuzz.sh).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stddef.h>
#include <stdint.h>

#include "aliro_assert.h"

// Accept-everything backend for the P-256 path. A real one rejects essentially
// every fuzzer input, which would leave the field parsing behind it unreached;
// this deliberately waves each frame through so the fuzzer explores the parse
// and policy code with attacker-controlled bytes -- the dangerous half.
static int always_ok(void *ctx, const uint8_t *msg, size_t msg_len,
		     const uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	(void)ctx;
	(void)msg;
	(void)msg_len;
	(void)sig;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/* Fixed nonce: the fuzzer explores framing/length/field paths. The
	 * accept-everything backend above is what keeps the parse and policy code
	 * behind authentication reachable; the happy path is covered by the KAT
	 * suite. */
	static const uint8_t nonce[ALIRO_ASSERT_NONCE_LEN] = { 0 };
	struct aliro_assert out;

	(void)aliro_assert_verify_p256(always_ok, NULL, data, size, nonce, 40, 0, &out);
	(void)aliro_assert_peek_alg(data, size);
	return 0;
}
