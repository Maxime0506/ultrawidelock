/*
 * aliro_assert_ec — binds the aliro_assert P-256 seam to aliro_prim.
 *
 * aliro_assert.c stays free of any crypto backend so it can remain portable
 * C11, model-checked and fuzzed. This is the other half: the two-function shim
 * that hands the seam whatever P-256 the platform already provides, so nothing
 * else has to know the seam exists.
 *
 * Split into its own translation unit rather than #ifdef'd into aliro_assert.c
 * because it is the only part with a dependency: a build that has no PSA (the
 * main host suite) simply does not compile this file, and the codec keeps its
 * harnesses either way.
 */
#pragma once

#include <stdint.h>

#include "aliro_assert.h"
#include "aliro_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Key material the two binders expect as their ctx. Pass a pointer to one of
 * these as the void *ctx argument of aliro_assert_build_p256 / _verify_p256. */
struct aliro_assert_ec_priv {
	uint8_t d[ALIRO_P256_SCALAR]; /* P-256 private scalar */
};

/**
 * Uncompressed ECDSA-P256 public key point: 0x04 || X || Y (65 bytes).
 */
struct aliro_assert_ec_pub {
	uint8_t q[ALIRO_ASSERT_PUB_LEN]; /* uncompressed point, 0x04 || X || Y */
};

/* aliro_assert_sign_fn over aliro_ecdsa_p256_sign. ctx is a
 * struct aliro_assert_ec_priv *. Returns 0 on success. */
int aliro_assert_ec_sign(void *ctx, const uint8_t *msg, size_t msg_len,
			 uint8_t sig[ALIRO_ASSERT_SIG_LEN]);

/* aliro_assert_verify_fn over aliro_ecdsa_p256_verify. ctx is a
 * struct aliro_assert_ec_pub *. Returns 0 when the signature is valid. */
int aliro_assert_ec_verify(void *ctx, const uint8_t *msg, size_t msg_len,
			   const uint8_t sig[ALIRO_ASSERT_SIG_LEN]);

#ifdef __cplusplus
}
#endif
