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
 * PSA_WANT_ALG_HKDF is absent. It depends on HMAC, which this image does not
 * enable, so it would resolve to nothing. Key derivation is aliro_hkdf() in
 * modules/woz_aliro/src/aliro_hash.c, pure C11 and already linked.
 *
 * PSA persistent-key storage is absent, and unlike the oracle this port can
 * afford that. The oracle must keep MBEDTLS_PSA_CRYPTO_STORAGE_C because
 * OPENTHREAD_CRYPTO_PSA=y puts Zephyr's crypto_psa.c in the call graph, and
 * that file imports keys at PSA_KEY_LIFETIME_PERSISTENT unconditionally. This
 * port leaves OpenThread on its upstream default of
 * OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS, so that caller does not exist, and no
 * tracked caller asks for a persistent lifetime: aliro_prim_psa.c imports,
 * uses and destroys volatile keys, and the reader identity is a record in the
 * key-value store rather than a PSA key. Adding OT PSA later means adding
 * storage back and a PSA ITS backend with it -- read the oracle's note before
 * assuming otherwise, because it cost a commissioning round to learn.
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
