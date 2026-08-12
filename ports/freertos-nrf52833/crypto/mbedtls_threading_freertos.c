/*
 * Mbed TLS mutex callbacks over FreeRTOS.
 *
 * Two tasks reach the PSA crypto core on this image: the OpenThread task, and
 * the task that runs the credential exchange behind NimBLE. They share the PSA key
 * store, its slot table and its DRBG state, so the library needs real locking
 * rather than the empty stubs a single-threaded image could get away with.
 *
 * Everything here is static. Mbed TLS declares its global mutexes as
 * file-scope objects and passes their addresses to a mutex_init callback that
 * returns void, so an allocating implementation would have no way to report
 * failure; xSemaphoreCreateMutexStatic() removes the question.
 *
 * These are plain mutexes, not recursive ones. Mbed TLS does not take a mutex
 * it already holds, and making them recursive would hide it if that ever
 * changed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nrfx.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <mbedtls/build_info.h>
#include <mbedtls/threading.h>

#include <ultrawidelock_freertos_crypto.h>
#include <ultrawidelock_freertos_platform.h>

static void mutex_init(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL) {
		return;
	}

	/*
	 * Mbed TLS re-initialises some contexts, and entropy.c goes further and
	 * zeroes the mutex storage before calling this (a CryptoCell
	 * workaround it applies unconditionally). Creating a second mutex over
	 * live storage would strand whatever the first one was protecting, so
	 * an already-created mutex is left alone.
	 */
	if (mutex->handle != NULL) {
		return;
	}

	mutex->handle = xSemaphoreCreateMutexStatic(&mutex->storage);
}

static void mutex_free(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL) {
		return;
	}

	/*
	 * Clearing the handle is the whole of the free. There is nothing to
	 * return to an allocator, and the library requires that a subsequent
	 * lock fail rather than succeed, which a NULL handle produces below.
	 */
	mutex->handle = NULL;
}

static int mutex_lock(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL || mutex->handle == NULL) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	/*
	 * Blocking is not available in an exception, and neither is the crypto
	 * this protects. Refusing here turns a call from the wrong context into
	 * an error the caller propagates, rather than a scheduler assert or a
	 * silently unprotected key store.
	 */
	if (__get_IPSR() != 0u) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	/*
	 * portMAX_DELAY, with no timeout path. A timeout would mean returning
	 * an error while another task still holds the key store, and every
	 * caller would then have to decide what to do about a key operation
	 * that neither happened nor failed for a cryptographic reason. Waiting
	 * is the honest answer: the longest hold here is one P-256 operation.
	 *
	 * Before the scheduler starts this still works. Only one context exists
	 * then, so the mutex is always free and the take returns without ever
	 * asking to block.
	 */
	if (xSemaphoreTake(mutex->handle, portMAX_DELAY) != pdTRUE) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	return 0;
}

static int mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
	if (mutex == NULL || mutex->handle == NULL) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	if (__get_IPSR() != 0u) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	if (xSemaphoreGive(mutex->handle) != pdTRUE) {
		return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
	}

	return 0;
}

void ultrawidelock_freertos_mbedtls_threading_init(void)
{
	static bool installed;

	/*
	 * mbedtls_threading_set_alt() initialises the library's own global
	 * mutexes as well as installing the callbacks, so calling it twice
	 * would re-init mutexes that are already protecting state. The guard in
	 * mutex_init above makes that survivable; this one makes it not happen.
	 */
	if (installed) {
		return;
	}
	installed = true;

	mbedtls_threading_set_alt(mutex_init, mutex_free, mutex_lock, mutex_unlock);
}
