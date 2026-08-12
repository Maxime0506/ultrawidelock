/*
 * Bringing the crypto provider up, in the one order that works.
 *
 * Threading callbacks first, then the PSA core. That is not stylistic:
 * mbedtls_threading_set_alt() initialises the library's own global mutexes as
 * part of installing the callbacks, and psa_crypto_init() takes two of them
 * (the key slot mutex and the RNG mutex) on its way through. Calling the core
 * first would have it lock mutexes that do not exist yet.
 *
 * Idempotent, because both callers want the core up and neither is reliably
 * first: the Aliro reader initialises crypto when it loads its identity, and
 * the OpenThread task initialises it when the stack starts. psa_crypto_init()
 * is itself documented as safe to call more than once, but the guard here also
 * keeps the log line to one.
 */
#include <stdbool.h>
#include <stddef.h>

#include <psa/crypto.h>

#include <woz_freertos_crypto.h>
#include <woz_freertos_platform.h>

#define CRYPTO_TAG "crypto"

int woz_freertos_crypto_init(void)
{
	static bool ready;
	psa_status_t status;

	if (ready) {
		return 0;
	}

	woz_freertos_mbedtls_threading_init();

	/*
	 * This is where the DRBG gets seeded, and it is the one call in the
	 * image that draws a large block from the hardware RNG. Bias correction
	 * costs on the order of a hundred microseconds a byte, so a cold seed
	 * is milliseconds of RNG time. It happens once, at startup, before the
	 * radio is carrying traffic -- which is why the seed goes through a
	 * DRBG at all rather than every psa_generate_random() reaching the
	 * peripheral.
	 */
	status = psa_crypto_init();
	if (status != PSA_SUCCESS) {
		/*
		 * Not fatal here. The caller decides: the reader cannot operate
		 * without crypto and will say so, but a build that only wanted
		 * Thread should not be reset out from under by this path.
		 */
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, CRYPTO_TAG, "psa_crypto_init failed (%d)",
				 (int)status);
		return -1;
	}

	ready = true;
	woz_freertos_log(WOZ_FREERTOS_LOG_INFO, CRYPTO_TAG, "PSA core ready (software P-256)");
	return 0;
}
