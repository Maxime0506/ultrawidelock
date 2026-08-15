/* SPDX-License-Identifier: ISC */

/*
 * The crypto backend this port selects, and the one call that starts it.
 *
 * The provider is Mbed TLS built standalone -- its own CMake, no Zephyr -- with
 * the PSA crypto core enabled. That choice is not about Mbed TLS being the
 * best software P-256 available; it is about which files already compile
 * against it. modules/ultrawidelock_cred/src/ultrawidelock_prim_psa.c is the reader's primitive
 * backend and it speaks PSA, so a PSA core means it is reused rather than
 * rewritten, exactly as ports/esp32 reuses it over ESP-IDF's Mbed TLS.
 *
 * OpenThread is deliberately NOT put on PSA. It stays on its upstream default,
 * OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS, which calls Mbed TLS directly. That
 * keeps Zephyr's crypto_psa.c out of the build, and with it the persistent-key
 * lifetime that would otherwise force MBEDTLS_PSA_CRYPTO_STORAGE_C and a PSA
 * ITS backend over the key-value store. OpenThread's key material lands in
 * otPlatSettings instead, which storage/kv_flash_freertos.c already backs.
 *
 * The consequence to know about: Thread credentials are stored differently
 * here than in the Zephyr oracle, so a board reflashed between the two images
 * loses them. That is the same class of divergence as the key-value format
 * itself, and for the same reason -- this port is a sibling of the oracle, not
 * a drop-in replacement for its flash contents.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_CRYPTO_H
#define ULTRAWIDELOCK_FREERTOS_CRYPTO_H

#include <stddef.h>

/*
 * Install the Mbed TLS threading callbacks. Must run before any other Mbed TLS
 * or PSA call, including psa_crypto_init(), because mbedtls_threading_set_alt()
 * initialises the library's own global mutexes as part of installing them.
 *
 * Calling this more than once is safe and does nothing after the first.
 */
void ultrawidelock_freertos_mbedtls_threading_init(void);

/*
 * Bring up the crypto provider: threading, then the PSA core. Returns zero on
 * success. Idempotent, because both the credential path and the OpenThread path
 * want the core up and neither can be sure it is first.
 */
int ultrawidelock_freertos_crypto_init(void);

/*
 * The FreeRTOS-heap allocator Mbed TLS uses is declared in
 * crypto/mbedtls_config_freertos.h and not here, because the macros naming it
 * expand inside library sources that read the config file and nothing else of
 * this port's.
 */

#endif /* ULTRAWIDELOCK_FREERTOS_CRYPTO_H */
