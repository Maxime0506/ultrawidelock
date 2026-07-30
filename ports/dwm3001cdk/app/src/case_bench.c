/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 0a: what a Matter CASE responder costs on this part.
 *
 * Every RAM figure in internal/cdk-matter-plan.md is an estimate except the
 * ones under the reader, and the single number the plan turns on -- the peak of
 * the Matter thread -- cannot be measured by `kernel thread stacks`, because
 * that reads threads which already exist and this one does not yet. Written
 * before stages 1-5 rather than after, because finding the peak at stage 6
 * costs months and finding it here costs an afternoon.
 *
 * Models the RESPONDER half of CASE, which is the expensive half and the half
 * this board would run:
 *
 *   Sigma1 in    -- destinationId HMAC, then
 *   Sigma2 out   -- ECDH(responder eph, initiator eph), HKDF -> S2K,
 *                   ECDSA sign over TBSData2, AES-CCM encrypt TBEData2
 *   Sigma3 in    -- AES-CCM decrypt TBEData3, then THREE ECDSA verifies:
 *                   ICAC signed by RCAC, NOC signed by ICAC, and TBSData3
 *                   signed by the initiator's NOC key
 *   session      -- HKDF -> I2R/R2I keys
 *
 * so five P-256 operations, and P-256 is what costs stack here: the nRF52833
 * has no CryptoCell, so PSA lands on the software oberon driver
 * (CONFIG_PSA_CRYPTO_DRIVER_OBERON) and every curve operation runs on the
 * calling thread's stack.
 *
 * WHAT IS MEASURED, and what is not:
 *   - measured: the crypto call depth, on a dedicated thread, and the mbedTLS
 *     heap those calls draw. Buffers are deliberately STATIC so the stack
 *     figure is the crypto's own depth rather than a consequence of where this
 *     file chose to put a certificate. Their total is reported separately, so
 *     a hand-written node can decide stack-vs-bss for itself.
 *   - NOT measured: Matter TLV certificate parsing. No parser exists yet
 *     (stage 1). If it is written recursively its depth adds to everything
 *     below; an iterative walk adds almost nothing. This is the residual
 *     unknown in the number this file prints.
 *
 * Every PSA status is checked. A crypto call that fails costs no stack, so an
 * unchecked failure would report a comfortably small peak that means nothing;
 * one failure here suppresses the result entirely rather than flattering it.
 *
 * Never on in a shipping image: CONFIG_ALIRO_CASE_BENCH defaults n.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <psa/crypto.h>

#include "aliro_hash.h"

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
#include <mbedtls/memory_buffer_alloc.h>
#endif

LOG_MODULE_REGISTER(case_bench, LOG_LEVEL_INF);

/* Generous on purpose. The point is to find the high-water mark, so the stack
 * must be larger than the answer; an overflow would trip the MPU guard and
 * report nothing. Sized down to the measurement afterwards. */
#define CASE_BENCH_STACK_SZ 16384

/* Sizes from the CASE responder flow. The certificate figures are Matter TLV
 * operational certs, which run 350-450 B; 450 is the pessimistic end. */
#define P256_PUB_LEN      65
#define P256_SIG_LEN      64
#define SHA256_LEN        32
#define AEAD_KEY_LEN      16
#define CERT_LEN          450
#define TBE_LEN           (2 * CERT_LEN + P256_SIG_LEN + 64)
#define TBS_LEN           (2 * P256_PUB_LEN + 64)

/* Static by design -- see the header. Reported as a working set, not folded
 * into the stack number. */
static uint8_t initiator_random[SHA256_LEN];
static uint8_t responder_random[SHA256_LEN];
static uint8_t initiator_eph_pub[P256_PUB_LEN];
static uint8_t shared_secret[SHA256_LEN];
static uint8_t transcript_hash[SHA256_LEN];
static uint8_t s2k[AEAD_KEY_LEN];
static uint8_t session_keys[2 * AEAD_KEY_LEN];
static uint8_t tbs_data[TBS_LEN];
static uint8_t tbe_plain[TBE_LEN];
static uint8_t tbe_cipher[TBE_LEN + 16];
static uint8_t sig_tbs2[P256_SIG_LEN];
static uint8_t sig_icac_by_rcac[P256_SIG_LEN];
static uint8_t sig_noc_by_icac[P256_SIG_LEN];
static uint8_t sig_tbs3[P256_SIG_LEN];
static uint8_t pub_rcac[P256_PUB_LEN];
static uint8_t pub_icac[P256_PUB_LEN];
static uint8_t pub_noc[P256_PUB_LEN];

#define WORKING_SET_BYTES                                                      \
	(sizeof(initiator_random) + sizeof(responder_random) +                 \
	 sizeof(initiator_eph_pub) + sizeof(shared_secret) +                   \
	 sizeof(transcript_hash) + sizeof(s2k) + sizeof(session_keys) +        \
	 sizeof(tbs_data) + sizeof(tbe_plain) + sizeof(tbe_cipher) +           \
	 sizeof(sig_tbs2) + sizeof(sig_icac_by_rcac) +                         \
	 sizeof(sig_noc_by_icac) + sizeof(sig_tbs3) + sizeof(pub_rcac) +       \
	 sizeof(pub_icac) + sizeof(pub_noc))

K_THREAD_STACK_DEFINE(case_bench_stack, CASE_BENCH_STACK_SZ);
static struct k_thread case_bench_tcb;

static bool bench_ok = true;

static void psa_check(const char *what, psa_status_t st)
{
	if (st != PSA_SUCCESS) {
		LOG_ERR("%s failed: %d", what, (int)st);
		bench_ok = false;
	}
}

/* An ECC key pair usable for one algorithm. The bench needs several: two
 * ephemeral pairs for the ECDH, and three signing pairs standing in for RCAC,
 * ICAC and the initiator's NOC. */
static psa_status_t make_ecc_key(psa_key_usage_t usage, psa_algorithm_t alg,
				 psa_key_id_t *out)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&attr, usage);
	psa_set_key_algorithm(&attr, alg);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);

	return psa_generate_key(&attr, out);
}

static psa_status_t import_pub(const uint8_t *pub, size_t len, psa_algorithm_t alg,
			       psa_key_id_t *out)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attr, alg);
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);

	return psa_import_key(&attr, pub, len, out);
}

/* HKDF-SHA256, the KDF CASE uses for S2K, S3K and the session keys.
 *
 * The repo's own aliro_hkdf(), not PSA key derivation, for two reasons. It is
 * what a hand-written node on this board would actually call: pure C11, already
 * linked, no extra flash. And PSA key derivation is not available in this image
 * at all -- PSA_WANT_ALG_HKDF depends on PSA_WANT_ALG_HMAC, which is off, so
 * psa_key_derivation_setup() returns PSA_ERROR_NOT_SUPPORTED (-134). The first
 * run of this bench found that the hard way; see prj.conf. */
static void hkdf(const uint8_t *secret, size_t secret_len, const uint8_t *salt,
		 size_t salt_len, const char *info, uint8_t *out, size_t out_len)
{
	if (aliro_hkdf(salt, salt_len, secret, secret_len, (const uint8_t *)info, strlen(info),
		       out, out_len) != 0) {
		LOG_ERR("aliro_hkdf failed");
		bench_ok = false;
	}
}

static void case_bench_run(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	const psa_algorithm_t sign_alg = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
	const psa_algorithm_t aead_alg = PSA_ALG_CCM;
	psa_key_id_t eph_resp = 0;
	psa_key_id_t eph_init = 0;
	psa_key_id_t k_rcac = 0;
	psa_key_id_t k_icac = 0;
	psa_key_id_t k_noc = 0;
	psa_key_id_t k_s2k = 0;
	psa_key_id_t v_rcac = 0;
	psa_key_id_t v_icac = 0;
	psa_key_id_t v_noc = 0;
	size_t olen;

	/* Let the reader finish coming up, so the bench runs against a live
	 * radio and a live BLE stack rather than an idle machine. */
	k_sleep(K_SECONDS(8));

	/* The reader already did this from main(); PSA tolerates the repeat and
	 * it keeps the bench standalone if the reader ever fails to start. */
	(void)psa_crypto_init();

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
	size_t heap_before = 0;
	size_t blocks_before = 0;

	mbedtls_memory_buffer_alloc_max_get(&heap_before, &blocks_before);
#endif

	psa_check("rng", psa_generate_random(initiator_random, sizeof(initiator_random)));
	psa_check("rng", psa_generate_random(responder_random, sizeof(responder_random)));
	psa_check("rng", psa_generate_random(transcript_hash, sizeof(transcript_hash)));
	psa_check("rng", psa_generate_random(tbs_data, sizeof(tbs_data)));
	psa_check("rng", psa_generate_random(tbe_plain, sizeof(tbe_plain)));

	/* Signing keys stand in for the three certificate keys plus the
	 * responder's own NOC key; generate first so their public halves are
	 * real points and the verifies do full curve work. */
	psa_check("gen rcac", make_ecc_key(PSA_KEY_USAGE_SIGN_MESSAGE, sign_alg, &k_rcac));
	psa_check("gen icac", make_ecc_key(PSA_KEY_USAGE_SIGN_MESSAGE, sign_alg, &k_icac));
	psa_check("gen noc", make_ecc_key(PSA_KEY_USAGE_SIGN_MESSAGE, sign_alg, &k_noc));
	psa_check("export rcac", psa_export_public_key(k_rcac, pub_rcac, sizeof(pub_rcac), &olen));
	psa_check("export icac", psa_export_public_key(k_icac, pub_icac, sizeof(pub_icac), &olen));
	psa_check("export noc", psa_export_public_key(k_noc, pub_noc, sizeof(pub_noc), &olen));

	/* Signatures the responder will have to check in Sigma3. Producing
	 * them is setup, not part of the measured flow, but it costs the same
	 * stack as the sign in Sigma2 so it cannot inflate the answer. */
	psa_check("presign icac", psa_sign_message(k_rcac, sign_alg, pub_icac, sizeof(pub_icac),
						   sig_icac_by_rcac, sizeof(sig_icac_by_rcac), &olen));
	psa_check("presign noc", psa_sign_message(k_icac, sign_alg, pub_noc, sizeof(pub_noc),
						  sig_noc_by_icac, sizeof(sig_noc_by_icac), &olen));
	psa_check("presign tbs3", psa_sign_message(k_noc, sign_alg, tbs_data, sizeof(tbs_data),
						   sig_tbs3, sizeof(sig_tbs3), &olen));

	uint32_t t0 = k_cycle_get_32();

	/* ---- Sigma2 out ---------------------------------------------- */
	psa_check("gen eph resp", make_ecc_key(PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH, &eph_resp));
	psa_check("gen eph init", make_ecc_key(PSA_KEY_USAGE_DERIVE, PSA_ALG_ECDH, &eph_init));
	psa_check("export eph init", psa_export_public_key(eph_init, initiator_eph_pub,
							   sizeof(initiator_eph_pub), &olen));
	/* 1. ECDH */
	psa_check("ecdh", psa_raw_key_agreement(PSA_ALG_ECDH, eph_resp, initiator_eph_pub, olen,
						shared_secret, sizeof(shared_secret), &olen));
	/* 2. HKDF -> S2K */
	hkdf(shared_secret, sizeof(shared_secret), transcript_hash, sizeof(transcript_hash),
	     "Sigma2", s2k, sizeof(s2k));
	/* 3. ECDSA sign over TBSData2 */
	psa_check("sign tbs2", psa_sign_message(k_noc, sign_alg, tbs_data, sizeof(tbs_data),
						sig_tbs2, sizeof(sig_tbs2), &olen));
	/* 4. AES-CCM encrypt TBEData2 */
	psa_key_attributes_t aead_attr = PSA_KEY_ATTRIBUTES_INIT;

	psa_set_key_usage_flags(&aead_attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
	psa_set_key_algorithm(&aead_attr, aead_alg);
	psa_set_key_type(&aead_attr, PSA_KEY_TYPE_AES);
	psa_set_key_bits(&aead_attr, 128);
	psa_check("import s2k", psa_import_key(&aead_attr, s2k, sizeof(s2k), &k_s2k));
	psa_check("ccm encrypt",
		  psa_aead_encrypt(k_s2k, aead_alg, transcript_hash, 13, NULL, 0, tbe_plain,
				   sizeof(tbe_plain), tbe_cipher, sizeof(tbe_cipher), &olen));

	/* ---- Sigma3 in ----------------------------------------------- */
	/* 5. AES-CCM decrypt TBEData3 */
	psa_check("ccm decrypt",
		  psa_aead_decrypt(k_s2k, aead_alg, transcript_hash, 13, NULL, 0, tbe_cipher, olen,
				   tbe_plain, sizeof(tbe_plain), &olen));
	/* 6-8. Three ECDSA verifies: the chain, then the signature over TBSData3. */
	psa_check("import v_rcac", import_pub(pub_rcac, sizeof(pub_rcac), sign_alg, &v_rcac));
	psa_check("import v_icac", import_pub(pub_icac, sizeof(pub_icac), sign_alg, &v_icac));
	psa_check("import v_noc", import_pub(pub_noc, sizeof(pub_noc), sign_alg, &v_noc));
	psa_check("verify icac<-rcac",
		  psa_verify_message(v_rcac, sign_alg, pub_icac, sizeof(pub_icac),
				     sig_icac_by_rcac, sizeof(sig_icac_by_rcac)));
	psa_check("verify noc<-icac",
		  psa_verify_message(v_icac, sign_alg, pub_noc, sizeof(pub_noc), sig_noc_by_icac,
				     sizeof(sig_noc_by_icac)));
	psa_check("verify tbs3", psa_verify_message(v_noc, sign_alg, tbs_data, sizeof(tbs_data),
						    sig_tbs3, sizeof(sig_tbs3)));

	/* ---- session keys -------------------------------------------- */
	hkdf(shared_secret, sizeof(shared_secret), transcript_hash, sizeof(transcript_hash),
	     "SessionKeys", session_keys, sizeof(session_keys));

	uint32_t elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

	(void)psa_destroy_key(eph_resp);
	(void)psa_destroy_key(eph_init);
	(void)psa_destroy_key(k_rcac);
	(void)psa_destroy_key(k_icac);
	(void)psa_destroy_key(k_noc);
	(void)psa_destroy_key(k_s2k);
	(void)psa_destroy_key(v_rcac);
	(void)psa_destroy_key(v_icac);
	(void)psa_destroy_key(v_noc);

	size_t unused = 0;
	int rc = k_thread_stack_space_get(k_current_get(), &unused);

	if (!bench_ok) {
		LOG_ERR("CASE bench FAILED a crypto step; the numbers below would "
			"be too small to mean anything. Not reporting.");
		return;
	}
	if (rc != 0) {
		LOG_ERR("k_thread_stack_space_get rc=%d (CONFIG_INIT_STACKS?)", rc);
		return;
	}

	LOG_INF("CASE responder: stack peak %u B of %u", (unsigned int)(CASE_BENCH_STACK_SZ - unused),
		(unsigned int)CASE_BENCH_STACK_SZ);
	LOG_INF("CASE responder: static working set %u B", (unsigned int)WORKING_SET_BYTES);
	LOG_INF("CASE responder: 5 P-256 ops + 2 HKDF + 2 AES-CCM in %u us", elapsed_us);

#if IS_ENABLED(CONFIG_ALIRO_HEAP_PROBE)
	size_t heap_after = 0;
	size_t blocks_after = 0;

	mbedtls_memory_buffer_alloc_max_get(&heap_after, &blocks_after);
	LOG_INF("CASE responder: mbedtls heap peak %u B (was %u before the bench)",
		(unsigned int)heap_after, (unsigned int)heap_before);
#endif
}

static int case_bench_start(void)
{
	(void)k_thread_create(&case_bench_tcb, case_bench_stack, CASE_BENCH_STACK_SZ,
			      case_bench_run, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
			      0, K_NO_WAIT);
	k_thread_name_set(&case_bench_tcb, "case_bench");
	return 0;
}

/* Lowest priority and an 8 s delay inside the thread, so the reader owns the
 * CPU while it starts and the bench cannot perturb the path 5b8d06b fought
 * for. */
SYS_INIT(case_bench_start, APPLICATION, 99);
