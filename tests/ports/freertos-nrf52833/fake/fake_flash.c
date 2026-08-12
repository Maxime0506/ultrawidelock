#include "fake_flash.h"

#include <string.h>
#include <sys/mman.h>

#include <ultrawidelock_freertos_platform.h>

uint8_t *fake_flash;
unsigned fake_flash_violations;
unsigned fake_flash_write_calls;
unsigned fake_flash_erase_calls;
unsigned fake_flash_fail_write_after;

void fake_flash_reset(void)
{
	if (fake_flash == NULL) {
		fake_flash = mmap(NULL, FAKE_FLASH_SIZE, PROT_READ | PROT_WRITE,
				  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	}
	fake_flash_blank();
	fake_flash_violations = 0;
	fake_flash_write_calls = 0;
	fake_flash_erase_calls = 0;
	fake_flash_fail_write_after = 0;
}

void fake_flash_blank(void)
{
	memset(fake_flash, 0xff, FAKE_FLASH_SIZE);
}

static int in_range(uint32_t offset, size_t length)
{
	if (offset < FAKE_FLASH_BASE) {
		return 0;
	}
	if ((offset - FAKE_FLASH_BASE) + length > FAKE_FLASH_SIZE) {
		return 0;
	}
	return 1;
}

int ultrawidelock_freertos_flash_read(uint32_t offset, void *buffer, size_t length)
{
	if (!in_range(offset, length)) {
		fake_flash_violations++;
		return -1;
	}
	memcpy(buffer, &fake_flash[offset - FAKE_FLASH_BASE], length);
	return 0;
}

int ultrawidelock_freertos_flash_write(uint32_t offset, const void *data, size_t length)
{
	const uint8_t *in = data;
	size_t i;
	size_t index;

	if (!in_range(offset, length)) {
		fake_flash_violations++;
		return -1;
	}
	if ((offset % ULTRAWIDELOCK_FREERTOS_FLASH_WRITE_ALIGN) != 0u ||
	    (length % ULTRAWIDELOCK_FREERTOS_FLASH_WRITE_ALIGN) != 0u) {
		fake_flash_violations++;
		return -1;
	}

	/* A write that would set a bit needs an erase first, and is refused. */
	index = offset - FAKE_FLASH_BASE;
	for (i = 0; i < length; i++) {
		if ((in[i] & ~fake_flash[index + i]) != 0u) {
			fake_flash_violations++;
			return -1;
		}
	}

	fake_flash_write_calls++;
	if (fake_flash_fail_write_after != 0u) {
		fake_flash_fail_write_after--;
		if (fake_flash_fail_write_after == 0u) {
			/* The write never reaches the part. */
			return -1;
		}
	}

	for (i = 0; i < length; i++) {
		fake_flash[index + i] &= in[i];
	}
	return 0;
}

int ultrawidelock_freertos_flash_erase(uint32_t offset, size_t length)
{
	if (!in_range(offset, length)) {
		fake_flash_violations++;
		return -1;
	}
	if (((offset - FAKE_FLASH_BASE) % ULTRAWIDELOCK_FREERTOS_FLASH_PAGE_SIZE) != 0u ||
	    (length % ULTRAWIDELOCK_FREERTOS_FLASH_PAGE_SIZE) != 0u) {
		fake_flash_violations++;
		return -1;
	}

	fake_flash_erase_calls++;
	memset(&fake_flash[offset - FAKE_FLASH_BASE], 0xff, length);
	return 0;
}
