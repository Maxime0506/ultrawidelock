/*
 * The PSA algorithm set this product actually uses.
 *
 * Every line here is the Zephyr oracle's CONFIG_PSA_WANT_* assignment with the
 * CONFIG_ prefix removed, because that is exactly what Zephyr's Kconfig does
 * with them. apps/dwm3001cdk-lock/prj.conf is the source, and its reasoning
 * for each inclusion and each deliberate omission is not repeated here.
 *
 * Two omissions carry over and matter:
 *
 * PSA_WANT_ALG_HKDF is absent. It depends on HMAC, and nothing here derives
 * keys through a PSA key derivation: aliro_hkdf() in
 * modules/woz_aliro/src/aliro_hash.c is pure C11 and already linked, and
 * OpenThread's own HKDF is built on its HmacSha256 rather than on PSA.
 *
 * PSA persistent-key storage depends on the build. A READER-ONLY image has no
 * caller that asks for a persistent lifetime -- aliro_prim_psa.c imports, uses
 * and destroys volatile keys, and the reader identity is a record in the
 * key-value store rather than a PSA key. A MATTER image does: it sets
 * OPENTHREAD_CONFIG_CRYPTO_LIB_PSA, which puts Nordic's crypto_psa.c in the
 * call graph, and that file imports keys at PSA_KEY_LIFETIME_PERSISTENT
 * unconditionally. That is why MBEDTLS_PSA_CRYPTO_STORAGE_C and the ITS backend
 * in crypto/psa_its_freertos.c exist. The oracle pays for storage always, for
 * the same reason and with no switch to avoid it.
 */
#ifndef WOZ_FREERTOS_PSA_CRYPTO_CONFIG_H
#define WOZ_FREERTOS_PSA_CRYPTO_CONFIG_H

#define PSA_WANT_ALG_ECDSA 1
#define PSA_WANT_ALG_ECDH 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_DERIVE 1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY 1
#define PSA_WANT_ECC_SECP_R1_256 1
#define PSA_WANT_ALG_SHA_256 1
#define PSA_WANT_ALG_CMAC 1
#define PSA_WANT_ALG_ECB_NO_PADDING 1
#define PSA_WANT_ALG_GCM 1
#define PSA_WANT_KEY_TYPE_AES 1

#if defined(WOZ_HAVE_MATTER) && WOZ_HAVE_MATTER
/*
 * WHAT OPENTHREAD NEEDS ONCE ITS CRYPTO LIBRARY IS PSA.
 *
 * These three are not in the oracle's list, and that is not an oversight in
 * either direction: the oracle leaves OpenThread on its upstream Mbed TLS
 * crypto library, where HMAC is reached through MBEDTLS_MD_C and never through
 * PSA. This port's Matter build sets OPENTHREAD_CONFIG_CRYPTO_LIB_PSA instead,
 * which routes the same calls into Nordic's crypto_psa.c, and that file asks
 * PSA for algorithms nothing else in this image had ever asked for.
 *
 * In Zephyr they arrive by themselves. nrf/subsys/nrf_security/Kconfig.legacy
 * carries "select PSA_WANT_ALG_HMAC if PSA_CRYPTO_CLIENT" and the matching line
 * for deterministic ECDSA, so a Kconfig build that enables OpenThread's PSA
 * crypto gets them without naming them. This port has no Kconfig, so nothing
 * selects anything, and a hand-written list omits exactly what it was never
 * told about.
 *
 * HOW THE OMISSION PRESENTED, because it is worth recognising again: the image
 * built clean, linked clean, booted, initialised the PSA core, brought up the
 * OpenThread radio -- and then the boot task spun forever inside
 * KeyManager::ComputeKeys. Thread derives its key material by HMAC-SHA256 over
 * the network key, psa_get_and_lock_key_slot_with_policy answered
 * PSA_ERROR_NOT_SUPPORTED, and OpenThread wraps that call in SuccessOrAssert,
 * which on this platform is an infinite loop rather than a message. The key in
 * ITS was correct, the policy was correct, and the log's last line was
 * "OpenThread radio initialized". Nothing named HMAC anywhere in the symptom.
 *
 * HMAC also carries HKDF. OpenThread's hkdf_sha256.cpp is written on top of
 * Crypto::HmacSha256 rather than on a PSA key derivation, so the note above
 * about PSA_WANT_ALG_HKDF being pointless without HMAC still holds and still
 * needs no line of its own.
 *
 * PSA_WANT_ALG_PBKDF2_AES_CMAC_PRF_128 stays out. crypto_psa.c references it,
 * but only from otPlatCryptoPbkdf2GenerateKey, which derives a PSKc for the
 * commissioner and joiner roles this lock does not build. If a joiner is ever
 * added, this is the line it will need, and it will announce itself the same
 * unhelpful way.
 */
#define PSA_WANT_ALG_HMAC 1
#define PSA_WANT_KEY_TYPE_HMAC 1
#define PSA_WANT_ALG_DETERMINISTIC_ECDSA 1
#endif

/*
 * The oracle's list has one more line, CONFIG_PSA_WANT_GENERATE_RANDOM, and it
 * is deliberately not carried over. That option is Nordic's, defined in
 * nrf/subsys/nrf_security/Kconfig.psa.nordic and nowhere in Mbed TLS: it
 * selects the Oberon driver's RNG entry point. Upstream Mbed TLS provides
 * psa_generate_random() whenever MBEDTLS_PSA_CRYPTO_C is on, so there is
 * nothing to ask for.
 *
 * Copying it anyway would have been silent. An unrecognised PSA_WANT is not an
 * error, it is simply never read, so the first symptom would have been a
 * runtime PSA_ERROR_NOT_SUPPORTED on the board had it actually gated anything.
 * freertos-crypto-source-check.sh now checks every option in this file against
 * include/psa/crypto_config.h for exactly that reason -- it is what caught it.
 */

#endif /* WOZ_FREERTOS_PSA_CRYPTO_CONFIG_H */
