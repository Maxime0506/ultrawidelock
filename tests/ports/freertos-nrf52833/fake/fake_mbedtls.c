/*
 * Mbed TLS and PSA doubles for the crypto backend.
 *
 * mbedtls_threading_set_alt() here does what the real one does and not merely
 * what the port needs it to do: it records the callbacks AND runs the init
 * callback over two global mutexes, standing in for the key-slot and
 * PSA-global-data mutexes the library owns. That is what makes the ordering
 * rule testable. A port that called psa_crypto_init() before installing the
 * callbacks would, on hardware, take mutexes that were never created; here the
 * same mistake leaves those two globals without handles, and the PSA double
 * refuses to succeed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <mbedtls/threading.h>
#include <psa/crypto.h>

unsigned fake_threading_set_alt_calls;
unsigned fake_psa_init_calls;

static struct fake_threading_callbacks s_installed;
static psa_status_t s_init_status = PSA_SUCCESS;
static bool s_threading_ready_at_init;

/*
 * Stand-ins for mbedtls_threading_key_slot_mutex and
 * mbedtls_threading_psa_globaldata_mutex, which the real set_alt initialises
 * and the real psa_crypto_init() then locks.
 */
static mbedtls_threading_mutex_t s_key_slot_mutex;
static mbedtls_threading_mutex_t s_globaldata_mutex;

void mbedtls_threading_set_alt(void (*mutex_init)(mbedtls_threading_mutex_t *),
			       void (*mutex_free)(mbedtls_threading_mutex_t *),
			       int (*mutex_lock)(mbedtls_threading_mutex_t *),
			       int (*mutex_unlock)(mbedtls_threading_mutex_t *))
{
	fake_threading_set_alt_calls++;

	s_installed.init = mutex_init;
	s_installed.free = mutex_free;
	s_installed.lock = mutex_lock;
	s_installed.unlock = mutex_unlock;

	if (mutex_init != NULL) {
		mutex_init(&s_key_slot_mutex);
		mutex_init(&s_globaldata_mutex);
	}
}

const struct fake_threading_callbacks *fake_threading_installed(void)
{
	return &s_installed;
}

void fake_threading_reset(void)
{
	fake_threading_set_alt_calls = 0;
	memset(&s_installed, 0, sizeof(s_installed));
	memset(&s_key_slot_mutex, 0, sizeof(s_key_slot_mutex));
	memset(&s_globaldata_mutex, 0, sizeof(s_globaldata_mutex));
}

psa_status_t psa_crypto_init(void)
{
	fake_psa_init_calls++;

	/*
	 * The core's first act is to take these. Recording whether they are
	 * usable is how the ordering rule bites: it is checked, not assumed.
	 */
	s_threading_ready_at_init = s_installed.lock != NULL &&
				    s_installed.lock(&s_key_slot_mutex) == 0 &&
				    s_installed.unlock(&s_key_slot_mutex) == 0 &&
				    s_installed.lock(&s_globaldata_mutex) == 0 &&
				    s_installed.unlock(&s_globaldata_mutex) == 0;

	if (!s_threading_ready_at_init) {
		return PSA_ERROR_INSUFFICIENT_MEMORY;
	}

	return s_init_status;
}

bool fake_psa_threading_was_ready(void)
{
	return s_threading_ready_at_init;
}

void fake_psa_reset(void)
{
	fake_psa_init_calls = 0;
	s_init_status = PSA_SUCCESS;
	s_threading_ready_at_init = false;
}

void fake_psa_set_init_status(psa_status_t status)
{
	s_init_status = status;
}
