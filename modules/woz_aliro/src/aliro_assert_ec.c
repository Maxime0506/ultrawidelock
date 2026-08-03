// Binds the aliro_assert P-256 seam to aliro_prim's ECDSA (see aliro_assert_ec.h).
// The only file in the presence path with a crypto-backend dependency, which is
// exactly why it is separate: aliro_assert.c keeps its cbmc and fuzz harnesses.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stddef.h>

#include "aliro_assert_ec.h"
#include "aliro_prim.h"

/* The seam and aliro_prim agree on every size, but a mismatch would be a buffer
 * overrun rather than a compile error, so it is checked at compile time. */
_Static_assert(ALIRO_ASSERT_SIG_LEN == ALIRO_P256_SIG, "assertion signature size != P-256 sig");
_Static_assert(ALIRO_ASSERT_PUB_LEN == ALIRO_P256_POINT, "assertion pubkey size != P-256 point");

/**
 * ECDSA-P256-SHA256 sign: hash msg internally and return 64-byte signature, or -1 on error.
 */
int aliro_assert_ec_sign(void *ctx, const uint8_t *msg, size_t msg_len,
			 uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	const struct aliro_assert_ec_priv *k = ctx;

	if (k == NULL || msg == NULL || sig == NULL) {
		return -1;
	}
	/* aliro_prim hashes internally (ECDSA-P256-SHA256 over the raw message),
	 * which is the contract the seam documents, so msg is passed through
	 * whole rather than pre-hashed here. */
	return aliro_ecdsa_p256_sign(k->d, msg, msg_len, sig);
}

/**
 * ECDSA-P256-SHA256 verify: return 0 if sig is valid over msg with the stored public point, -1 if
 * invalid or on error.
 */
int aliro_assert_ec_verify(void *ctx, const uint8_t *msg, size_t msg_len,
			   const uint8_t sig[ALIRO_ASSERT_SIG_LEN])
{
	const struct aliro_assert_ec_pub *k = ctx;

	if (k == NULL || msg == NULL || sig == NULL) {
		return -1;
	}
	return aliro_ecdsa_p256_verify(k->q, msg, msg_len, sig);
}
