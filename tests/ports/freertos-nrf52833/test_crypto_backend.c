/*
 * The crypto backend: the Mbed TLS threading callbacks, the FreeRTOS-heap
 * allocator, the hardware entropy poll, and the bring-up order.
 *
 * The mutex model enforces exclusion rather than accepting every take, so a
 * lock that forgot to check its return value is visible here as a second
 * caller walking into a held key store. The IPSR model is the same one the
 * flash driver's refusal is tested against; crypto mutexes have the same
 * problem for a different reason -- blocking is not available in an exception
 * and neither is the key store's consistency.
 *
 * Bring-up runs in its own forked process per scenario. Both the threading
 * install and the PSA readiness flag are one-shot statics, and a test that
 * reset the doubles underneath them would be asking whether the guard works
 * while standing on the wrong side of it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nrfx.h>

#include <FreeRTOS.h>

#include "fake_freertos.h"

#include <mbedtls/build_info.h>
#include <mbedtls/entropy.h>
#include <mbedtls/threading.h>
#include <psa/crypto.h>

#include <library/entropy_poll.h>

#include <ultrawidelock_freertos_crypto.h>
#include <ultrawidelock_freertos_platform.h>

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                    \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                              \
			printf("  FAIL %s\n", (label));                                            \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                            \
		}                                                                                  \
	} while (0)

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	printf("  FAIL unexpected fatal: %s\n", reason);
	fflush(stdout);
	_Exit(1);
}

/* ---- entropy source under the poll ---------------------------------------- */

static bool s_entropy_fails;
static unsigned s_entropy_calls;
static size_t s_entropy_last_len;

int ultrawidelock_freertos_entropy(void *buffer, size_t length)
{
	s_entropy_calls++;
	s_entropy_last_len = length;

	if (s_entropy_fails) {
		return -1;
	}

	/*
	 * A recognisable pattern rather than zeroes: a poll that reported a
	 * length it had not filled would otherwise be indistinguishable from
	 * one that filled a zeroed buffer.
	 */
	memset(buffer, 0xa5, length);
	return 0;
}

/* ---- the installed callbacks ---------------------------------------------- */

static const struct fake_threading_callbacks *callbacks(void)
{
	ultrawidelock_freertos_mbedtls_threading_init();
	return fake_threading_installed();
}

/* ---- scenarios ------------------------------------------------------------ */

static void scenario_install(void)
{
	const struct fake_threading_callbacks *cb = callbacks();

	CHECK("set_alt ran once", fake_threading_set_alt_calls == 1u);
	CHECK("an init callback is installed", cb->init != NULL);
	CHECK("a free callback is installed", cb->free != NULL);
	CHECK("a lock callback is installed", cb->lock != NULL);
	CHECK("an unlock callback is installed", cb->unlock != NULL);

	/*
	 * set_alt initialises the library's own global mutexes as it installs,
	 * so calling it twice would re-init mutexes that are already guarding
	 * state. The guard is what stops that, and the count is the evidence.
	 */
	ultrawidelock_freertos_mbedtls_threading_init();
	ultrawidelock_freertos_mbedtls_threading_init();
	CHECK("a repeated install is refused", fake_threading_set_alt_calls == 1u);
}

static void scenario_lock_cycle(void)
{
	const struct fake_threading_callbacks *cb = callbacks();
	mbedtls_threading_mutex_t mutex;

	memset(&mutex, 0, sizeof(mutex));

	CHECK("an uninitialised mutex has no handle", mutex.handle == NULL);
	cb->init(&mutex);
	CHECK("init creates a handle", mutex.handle != NULL);
	CHECK("the handle addresses the embedded storage",
	      (void *)mutex.handle == (void *)&mutex.storage);

	CHECK("lock succeeds", cb->lock(&mutex) == 0);
	CHECK("unlock succeeds", cb->unlock(&mutex) == 0);
	CHECK("the pair can be repeated", cb->lock(&mutex) == 0 && cb->unlock(&mutex) == 0);
}

static void scenario_exclusion(void)
{
	const struct fake_threading_callbacks *cb = callbacks();
	mbedtls_threading_mutex_t mutex;

	memset(&mutex, 0, sizeof(mutex));
	cb->init(&mutex);

	CHECK("the first lock is granted", cb->lock(&mutex) == 0);

	/*
	 * On hardware the second caller blocks here; the model reports the
	 * refusal instead. Either way the value the port must not do is ignore
	 * it and return success into a held key store.
	 */
	CHECK("a contended lock is not silently granted",
	      cb->lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);

	CHECK("the holder can still unlock", cb->unlock(&mutex) == 0);

	/* And an unlock with nothing held is an error, not a free extra count. */
	CHECK("unlocking an unheld mutex errors",
	      cb->unlock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
}

static void scenario_free(void)
{
	const struct fake_threading_callbacks *cb = callbacks();
	mbedtls_threading_mutex_t mutex;
	SemaphoreHandle_t first;

	memset(&mutex, 0, sizeof(mutex));

	cb->init(&mutex);
	first = mutex.handle;

	/*
	 * entropy.c re-initialises a context's mutex, and zeroes the storage
	 * first as a vendor workaround. A second create over live storage would
	 * strand whatever the first was protecting -- so the check has to be
	 * made with the mutex HELD. Comparing handles alone proves nothing:
	 * xSemaphoreCreateMutexStatic hands back the address of the storage it
	 * was given, so a second create returns the same pointer with the count
	 * silently restored, and the intruder walks in.
	 */
	CHECK("the mutex can be held", cb->lock(&mutex) == 0);
	cb->init(&mutex);
	CHECK("a repeated init keeps the original mutex", mutex.handle == first);
	CHECK("a repeated init does not release the holder",
	      cb->lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);

	CHECK("unlock before free succeeds", cb->unlock(&mutex) == 0);

	cb->free(&mutex);
	CHECK("free clears the handle", mutex.handle == NULL);
	CHECK("lock after free errors", cb->lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
	CHECK("unlock after free errors", cb->unlock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);

	/* A NULL mutex reaches these too, and must not be dereferenced. */
	cb->init(NULL);
	cb->free(NULL);
	CHECK("lock of NULL errors", cb->lock(NULL) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
	CHECK("unlock of NULL errors", cb->unlock(NULL) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
}

static void scenario_isr_refusal(void)
{
	const struct fake_threading_callbacks *cb = callbacks();
	mbedtls_threading_mutex_t mutex;
	unsigned takes_before;
	unsigned gives_before;

	memset(&mutex, 0, sizeof(mutex));
	cb->init(&mutex);

	fake_ipsr_set(16u);

	takes_before = mutex.storage.takes;
	CHECK("lock from an exception errors",
	      cb->lock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
	CHECK("lock from an exception did not touch the semaphore",
	      mutex.storage.takes == takes_before);

	gives_before = mutex.storage.gives;
	CHECK("unlock from an exception errors",
	      cb->unlock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
	CHECK("unlock from an exception did not touch the semaphore",
	      mutex.storage.gives == gives_before);

	fake_ipsr_set(0u);
	CHECK("the mutex still works from thread mode", cb->lock(&mutex) == 0);
	CHECK("and unlocks", cb->unlock(&mutex) == 0);
}

static void scenario_alloc(void)
{
	unsigned char *block;
	size_t i;
	bool zeroed = true;

	block = ultrawidelock_freertos_mbedtls_calloc(8u, 4u);
	CHECK("calloc returns a block", block != NULL);
	if (block != NULL) {
		for (i = 0; i < 32u; i++) {
			if (block[i] != 0u) {
				zeroed = false;
			}
		}
		CHECK("calloc zeroes what it returns", zeroed);
		ultrawidelock_freertos_mbedtls_free(block);
	}

	CHECK("a zero count allocates nothing",
	      ultrawidelock_freertos_mbedtls_calloc(0u, 4u) == NULL);
	CHECK("a zero size allocates nothing",
	      ultrawidelock_freertos_mbedtls_calloc(4u, 0u) == NULL);

	/*
	 * The overflow check is the reason this is a function and not a macro.
	 * Mbed TLS sizes bignum allocations from lengths an attacker can move,
	 * and a wrapped multiply hands back a block smaller than the caller is
	 * about to write into.
	 */
	CHECK("an overflowing product allocates nothing",
	      ultrawidelock_freertos_mbedtls_calloc(SIZE_MAX / 2u + 2u, 2u) == NULL);

	/*
	 * The step below the overflow. This one passes the check and is refused
	 * by the heap instead, which is what pins the boundary: a check written
	 * with >= rather than > would reject it here and never be noticed,
	 * because both paths return NULL.
	 */
	fake_malloc_calls = 0;
	CHECK("the largest non-overflowing product still reaches the heap",
	      ultrawidelock_freertos_mbedtls_calloc(SIZE_MAX / 2u, 2u) == NULL &&
		      fake_malloc_calls == 1u);

	/* free tolerates NULL, because the library passes it. */
	ultrawidelock_freertos_mbedtls_free(NULL);
	CHECK("freeing NULL is survivable", true);
}

static void scenario_entropy_poll(void)
{
	unsigned char buffer[24];
	size_t olen = 0xdeadu;
	int rc;

	memset(buffer, 0, sizeof(buffer));
	s_entropy_fails = false;
	s_entropy_calls = 0;

	rc = mbedtls_hardware_poll(NULL, buffer, sizeof(buffer), &olen);
	CHECK("a good poll succeeds", rc == 0);
	CHECK("a good poll reports the full length", olen == sizeof(buffer));
	CHECK("a good poll asked the board for that length", s_entropy_last_len == sizeof(buffer));
	CHECK("a good poll filled the buffer", buffer[0] == 0xa5u &&
					       buffer[sizeof(buffer) - 1u] == 0xa5u);

	/*
	 * All or nothing. Reporting a short length would credit the accumulator
	 * for entropy it never received, which is the failure that produces a
	 * seeded-looking DRBG with less entropy than it believes.
	 */
	memset(buffer, 0, sizeof(buffer));
	olen = 0xdeadu;
	s_entropy_fails = true;
	rc = mbedtls_hardware_poll(NULL, buffer, sizeof(buffer), &olen);
	CHECK("a failed poll reports the failure", rc == MBEDTLS_ERR_ENTROPY_SOURCE_FAILED);
	CHECK("a failed poll credits no entropy", olen == 0u);

	s_entropy_fails = false;
	olen = 0xdeadu;
	s_entropy_calls = 0;
	rc = mbedtls_hardware_poll(NULL, buffer, 0u, &olen);
	CHECK("a zero-length poll succeeds", rc == 0);
	CHECK("a zero-length poll credits nothing", olen == 0u);
	CHECK("a zero-length poll does not wake the RNG", s_entropy_calls == 0u);

	olen = 0xdeadu;
	CHECK("a NULL output errors",
	      mbedtls_hardware_poll(NULL, NULL, sizeof(buffer), &olen) ==
		      MBEDTLS_ERR_ENTROPY_SOURCE_FAILED);
	CHECK("a NULL length errors",
	      mbedtls_hardware_poll(NULL, buffer, sizeof(buffer), NULL) ==
		      MBEDTLS_ERR_ENTROPY_SOURCE_FAILED);
}

static void scenario_bringup(void)
{
	CHECK("bring-up succeeds", ultrawidelock_freertos_crypto_init() == 0);
	CHECK("the PSA core was initialised", fake_psa_init_calls == 1u);
	CHECK("threading was installed first", fake_psa_threading_was_ready());
	CHECK("the threading callbacks are in place", fake_threading_set_alt_calls == 1u);

	CHECK("a second bring-up is a no-op", ultrawidelock_freertos_crypto_init() == 0);
	CHECK("and does not re-enter the PSA core", fake_psa_init_calls == 1u);
	CHECK("and does not re-install the callbacks", fake_threading_set_alt_calls == 1u);
}

static void scenario_bringup_failure(void)
{
	fake_psa_set_init_status(PSA_ERROR_INSUFFICIENT_MEMORY);

	CHECK("a failing PSA core fails bring-up", ultrawidelock_freertos_crypto_init() == -1);
	CHECK("the core was reached", fake_psa_init_calls == 1u);
	CHECK("threading was still installed first", fake_psa_threading_was_ready());

	/*
	 * A failed bring-up must not latch. The heap is the plausible reason
	 * for the failure and it is a condition that can pass, so the next
	 * caller has to get a real attempt rather than a cached success.
	 */
	fake_psa_set_init_status(PSA_SUCCESS);
	CHECK("a retry is attempted", ultrawidelock_freertos_crypto_init() == 0);
	CHECK("the retry reached the core", fake_psa_init_calls == 2u);
	CHECK("the retry did not re-install the callbacks", fake_threading_set_alt_calls == 1u);
}

/* ---- driver --------------------------------------------------------------- */

enum {
	SCENARIO_INSTALL,
	SCENARIO_LOCK_CYCLE,
	SCENARIO_EXCLUSION,
	SCENARIO_FREE,
	SCENARIO_ISR_REFUSAL,
	SCENARIO_ALLOC,
	SCENARIO_ENTROPY_POLL,
	SCENARIO_BRINGUP,
	SCENARIO_BRINGUP_FAILURE,
	SCENARIO_COUNT,
};

static int run_scenario(int scenario)
{
	fake_threading_reset();
	fake_psa_reset();
	fake_ipsr_set(0u);

	switch (scenario) {
	case SCENARIO_INSTALL:
		scenario_install();
		break;
	case SCENARIO_LOCK_CYCLE:
		scenario_lock_cycle();
		break;
	case SCENARIO_EXCLUSION:
		scenario_exclusion();
		break;
	case SCENARIO_FREE:
		scenario_free();
		break;
	case SCENARIO_ISR_REFUSAL:
		scenario_isr_refusal();
		break;
	case SCENARIO_ALLOC:
		scenario_alloc();
		break;
	case SCENARIO_ENTROPY_POLL:
		scenario_entropy_poll();
		break;
	case SCENARIO_BRINGUP:
		scenario_bringup();
		break;
	default:
		scenario_bringup_failure();
		break;
	}

	printf("RESULT-PART: %u checks\n", g_checks);
	return g_failures == 0 ? 0 : 1;
}

static bool run_child(int scenario)
{
	pid_t pid;
	int status = 0;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int rc = run_scenario(scenario);

		fflush(stdout);
		_exit(rc);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		printf("  FAIL could not fork scenario %d\n", scenario);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	unsigned failures = 0;
	int scenario;

	for (scenario = 0; scenario < SCENARIO_COUNT; scenario++) {
		failures += run_child(scenario) ? 0u : 1u;
	}

	printf("RESULT: %s (%d scenarios)\n", failures == 0 ? "PASS" : "FAIL", SCENARIO_COUNT);
	return failures == 0 ? 0 : 1;
}
