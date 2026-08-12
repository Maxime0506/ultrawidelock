/*
 * The two platform services Mbed TLS cannot supply itself on this image:
 * an allocator, and a seed.
 *
 * Allocation goes to the FreeRTOS heap rather than to newlib's. NimBLE's
 * porting layer already forces a FreeRTOS heap into this image, so using it
 * here adds no second allocator and no second arena to size. mbedtls_config
 * points MBEDTLS_PLATFORM_CALLOC_MACRO straight at these, so there is no
 * registration step and therefore no window in which an unregistered default
 * could be called.
 *
 * Seeding goes to the board entropy source. MBEDTLS_NO_PLATFORM_ENTROPY and
 * MBEDTLS_ENTROPY_HARDWARE_ALT together make mbedtls_hardware_poll() below the
 * only source the entropy accumulator has, which is what a part with no
 * filesystem and no operating-system CSPRNG requires.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>

#include <mbedtls/build_info.h>
#include <mbedtls/entropy.h>

#include <library/entropy_poll.h>

#include <ultrawidelock_freertos_crypto.h>
#include <ultrawidelock_freertos_platform.h>

void *ultrawidelock_freertos_mbedtls_calloc(size_t count, size_t size)
{
	size_t bytes;
	void *block;

	if (count == 0u || size == 0u) {
		return NULL;
	}

	/*
	 * The multiply is the reason this wrapper exists rather than a macro
	 * around pvPortMalloc. Mbed TLS sizes bignum allocations from
	 * attacker-influenced lengths, and an overflow here would hand back a
	 * block smaller than the caller is about to write.
	 */
	if (count > SIZE_MAX / size) {
		return NULL;
	}
	bytes = count * size;

	block = pvPortMalloc(bytes);
	if (block == NULL) {
		return NULL;
	}

	/* pvPortMalloc does not zero; calloc's contract says it must. */
	memset(block, 0, bytes);
	return block;
}

void ultrawidelock_freertos_mbedtls_free(void *block)
{
	/* vPortFree already tolerates NULL, but stating it keeps the pair symmetric. */
	if (block == NULL) {
		return;
	}

	vPortFree(block);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;

	if (output == NULL || olen == NULL) {
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	}

	*olen = 0u;
	if (len == 0u) {
		return 0;
	}

	/*
	 * All or nothing. Reporting a short read would let the accumulator
	 * credit this source for entropy it did not receive, and the board hook
	 * either fills the buffer or fails.
	 */
	if (ultrawidelock_freertos_entropy(output, len) != 0) {
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	}

	*olen = len;
	return 0;
}
