/* psafake — test-side control/inspection API for the fake PSA Crypto and
 * mbedTLS-AES surfaces under tests/host/psafake/{psa,mbedtls}/.
 *
 * THE FAKE DOES NO CRYPTO. Every psa_* / mbedtls_* entry point records its
 * arguments (key bits, algorithm, usage, lengths) and serves deterministic
 * filler bytes; every return status is a knob. The suites built on it prove
 * only that the code under test plumbs the right parameters into the API and
 * takes the right branch on each failure — never that any ciphertext is
 * correct. */
#ifndef WOZ_PSAFAKE_H
#define WOZ_PSAFAKE_H

#include <stddef.h>
#include <stdint.h>

#define PSAFAKE_MAX_KEY 65u

struct psafake_state {
	/* knobs: injected return statuses (0 = PSA_SUCCESS / mbedTLS 0) */
	int32_t init_ret;
	int32_t random_ret;
	int32_t import_ret;
	int32_t cipher_ret;
	int32_t aead_enc_ret;
	int32_t aead_dec_ret;
	int32_t generate_key_ret;
	int32_t export_key_ret;
	int32_t export_pub_ret;
	int32_t raw_ka_ret;
	int32_t sign_ret;
	int32_t verify_ret;
	/* knobs: output-length overrides (-1 = natural length) */
	long cipher_olen;
	long aead_enc_olen;
	long aead_dec_olen;
	long export_olen;
	long export_pub_olen;
	long raw_ka_olen;
	long sign_olen;

	/* recorded: last key attributes at import/generate */
	uint32_t attr_usage;
	uint32_t attr_alg;
	uint32_t attr_type;
	size_t attr_bits;
	/* recorded: imported key material + call tallies */
	uint8_t key[PSAFAKE_MAX_KEY];
	size_t key_len;
	unsigned init_calls, random_calls, import_calls, cipher_calls;
	unsigned aead_enc_calls, aead_dec_calls, generate_calls;
	unsigned export_calls, export_pub_calls, raw_ka_calls;
	unsigned sign_calls, verify_calls, destroy_calls;
	uint32_t last_destroyed; /* key id handed to psa_destroy_key */
	uint32_t last_alg;       /* alg argument of the last operation call */
	size_t last_nonce_len, last_aad_len, last_in_len, last_random_len;
	size_t last_msg_len, last_sig_len;

	/* mbedTLS side */
	int32_t mtls_setkey_ret;
	int32_t mtls_crypt_ret;
	unsigned mtls_init_calls, mtls_free_calls, mtls_setkey_calls, mtls_crypt_calls;
	unsigned mtls_keybits;
	int mtls_mode;
};

extern struct psafake_state psafake;

/** @brief Zero all recordings, restore every knob to success/natural length. */
void psafake_reset(void);

#endif /* WOZ_PSAFAKE_H */
