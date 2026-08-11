/*
 * Internal flash on NVMC.
 *
 * Two halves, and only one of them is here. The NVMC half is this part's own
 * rules: a write may only clear bits, writes are word-aligned and a whole
 * number of words, an erase covers a whole page, and the controller has to be
 * put into write or erase mode and back to read mode around each operation.
 * That half is below and is complete.
 *
 * The other half is arbitration against the radio. Programming this flash
 * stalls the CPU, and a page erase stalls it for far longer than any radio
 * event can wait, so a write issued while MPSL has the radio scheduled will
 * overrun that event. The Zephyr oracle solves it by taking an MPSL timeslot
 * per operation, from a session the flash driver opens for itself. This port
 * does not have that binding yet, so window_begin refuses rather than
 * proceeding: a provisioning write that fails loudly is recoverable, and one
 * that silently corrupts a radio event is a lock that drops its connection
 * under load and cannot be diagnosed from the outside.
 *
 * Writes are confined to a window that excludes the application image by
 * default. A store with an offset bug can then lose its own data but cannot
 * erase the firmware out from under a door lock.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hal/nrf_nvmc.h>
#include <nrfx.h>

#include <woz_freertos_platform.h>
#include <woz_freertos_radio.h>

#define FLASH_TAG "flash"

/* The part's whole program flash. Reads are allowed anywhere inside it. */
#define FLASH_DEVICE_BASE 0x00000000u
#define FLASH_DEVICE_SIZE (512u * 1024u)

/*
 * What may be written or erased. The default is the two pages the key-value
 * store owns; a board that adds a DFU slot widens it deliberately rather than
 * by accident.
 */
#ifndef WOZ_FREERTOS_FLASH_WRITABLE_BASE
#define WOZ_FREERTOS_FLASH_WRITABLE_BASE 0x0007e000u
#endif
#ifndef WOZ_FREERTOS_FLASH_WRITABLE_LIMIT
#define WOZ_FREERTOS_FLASH_WRITABLE_LIMIT 0x00080000u
#endif

/*
 * The address a flash offset maps to. On the part they are the same, because
 * program flash is memory-mapped from zero and reads never touch the
 * controller. The host test points this at its model, which is the only way to
 * exercise the read path off target.
 */
#ifndef WOZ_FREERTOS_FLASH_MAPPED
#define WOZ_FREERTOS_FLASH_MAPPED(offset) ((const void *)(uintptr_t)(offset))
#endif

static bool in_device(uint32_t offset, size_t length)
{
	if (length == 0u) {
		return true;
	}
	if (offset < FLASH_DEVICE_BASE || offset > FLASH_DEVICE_BASE + FLASH_DEVICE_SIZE) {
		return false;
	}
	return length <= (FLASH_DEVICE_BASE + FLASH_DEVICE_SIZE) - offset;
}

static bool in_writable_window(uint32_t offset, size_t length)
{
	if (offset < WOZ_FREERTOS_FLASH_WRITABLE_BASE) {
		return false;
	}
	return length <= WOZ_FREERTOS_FLASH_WRITABLE_LIMIT - offset;
}

/*
 * Claim the flash controller against radio activity.
 *
 * With the radio down there is nothing to arbitrate and the operation may
 * proceed. With it up this needs an MPSL timeslot long enough for the
 * operation, taken from a session opened for the purpose, and that binding is
 * not written yet. Refusing is the honest failure: the caller reports an I/O
 * error and retries later, which is recoverable, whereas proceeding would
 * overrun whatever radio event MPSL had scheduled.
 */
static int window_begin(void)
{
	if (!woz_freertos_radio_ready()) {
		return 0;
	}
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, FLASH_TAG,
			 "refused: flash needs an MPSL timeslot while the radio is up");
	return -1;
}

static void window_end(void)
{
}

/* Spin until the controller has finished. NVMC stalls the CPU regardless. */
static void wait_ready(void)
{
	while (!nrf_nvmc_ready_check(NRF_NVMC)) {
		/* Spin. */
	}
}

int woz_freertos_flash_read(uint32_t offset, void *buffer, size_t length)
{
	if (buffer == NULL && length != 0u) {
		return -1;
	}
	if (!in_device(offset, length)) {
		return -1;
	}
	/* Flash is memory-mapped for reading; the controller is not involved. */
	memcpy(buffer, WOZ_FREERTOS_FLASH_MAPPED(offset), length);
	return 0;
}

int woz_freertos_flash_write(uint32_t offset, const void *data, size_t length)
{
	const uint8_t *in = data;
	size_t written;
	int rc;

	if (data == NULL && length != 0u) {
		return -1;
	}
	if (!in_device(offset, length) || !in_writable_window(offset, length)) {
		return -1;
	}
	if ((offset % WOZ_FREERTOS_FLASH_WRITE_ALIGN) != 0u ||
	    (length % WOZ_FREERTOS_FLASH_WRITE_ALIGN) != 0u) {
		return -1;
	}
	if (length == 0u) {
		return 0;
	}

	rc = window_begin();
	if (rc != 0) {
		return rc;
	}

	nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_WRITE);
	for (written = 0; written < length; written += WOZ_FREERTOS_FLASH_WRITE_ALIGN) {
		uint32_t word;

		/*
		 * Assembled rather than dereferenced. The caller is not
		 * required to align its buffer, and casting a byte pointer to
		 * uint32_t would break strict aliasing and rely on the core
		 * tolerating an unaligned load; this core tolerates one for a
		 * plain LDR but not for the multiple-register forms a compiler
		 * is free to emit here. The copy costs nothing once aligned.
		 */
		memcpy(&word, &in[written], sizeof(word));
		nrf_nvmc_word_write(offset + (uint32_t)written, word);
		wait_ready();
	}
	nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_READONLY);
	wait_ready();

	window_end();
	return 0;
}

int woz_freertos_flash_erase(uint32_t offset, size_t length)
{
	size_t erased;
	int rc;

	if (!in_device(offset, length) || !in_writable_window(offset, length)) {
		return -1;
	}
	if ((offset % WOZ_FREERTOS_FLASH_PAGE_SIZE) != 0u ||
	    (length % WOZ_FREERTOS_FLASH_PAGE_SIZE) != 0u) {
		return -1;
	}
	if (length == 0u) {
		return 0;
	}

	rc = window_begin();
	if (rc != 0) {
		return rc;
	}

	nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_ERASE);
	for (erased = 0; erased < length; erased += WOZ_FREERTOS_FLASH_PAGE_SIZE) {
		nrf_nvmc_page_erase_start(NRF_NVMC, offset + (uint32_t)erased);
		wait_ready();
	}
	nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_READONLY);
	wait_ready();

	window_end();
	return 0;
}
