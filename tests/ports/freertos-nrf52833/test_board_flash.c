/*
 * The board's flash hooks on NVMC.
 *
 * The controller model refuses a write outside write mode, an erase outside
 * erase mode, a write that would set a bit, and a misaligned address. Those are
 * the rules a driver gets wrong once and then corrupts flash with, so the test
 * asserts the violation counter stays at zero as well as asserting the results.
 *
 * The radio-arbitration half is not implemented, and what is checked here is
 * that its absence is a refusal rather than a write issued underneath a live
 * radio.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hal/nrf_nvmc.h>

#include <woz_freertos_platform.h>

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

static unsigned g_error_logs;

void woz_freertos_log(enum woz_freertos_log_level level, const char *tag, const char *fmt, ...)
{
	(void)tag;
	(void)fmt;
	if (level == WOZ_FREERTOS_LOG_ERROR) {
		g_error_logs++;
	}
}

/* The radio's readiness is what decides whether arbitration is needed. */
static bool g_radio_ready;

bool woz_freertos_radio_ready(void)
{
	return g_radio_ready;
}

/* The key-value store's two pages, which are the default writable window. */
#define KV_BASE 0x7e000u
#define KV_LIMIT 0x80000u
#define PAGE 4096u

static void reset_all(void)
{
	fake_nvmc_reset();
	g_radio_ready = false;
	g_error_logs = 0;
}

/* ---- reads -------------------------------------------------------------- */

static void test_read(void)
{
	uint8_t out[16];

	reset_all();
	memcpy(&fake_nvmc_flash[KV_BASE], "provisioned", 11);

	memset(out, 0, sizeof(out));
	CHECK("a read succeeds", woz_freertos_flash_read(KV_BASE, out, 11) == 0);
	CHECK("a read returns what is stored", memcmp(out, "provisioned", 11) == 0);

	/* Reads are allowed anywhere in the device, not just the write window. */
	CHECK("a read outside the write window succeeds",
	      woz_freertos_flash_read(0x1000u, out, 16) == 0);

	CHECK("a read past the end of the part is refused",
	      woz_freertos_flash_read(FAKE_NVMC_FLASH_SIZE - 4u, out, 16) != 0);
	CHECK("a null read buffer is refused", woz_freertos_flash_read(KV_BASE, NULL, 4) != 0);
	CHECK("a zero-length read succeeds", woz_freertos_flash_read(KV_BASE, out, 0) == 0);

	CHECK("reading never touched the controller", fake_nvmc.mode_changes == 0u);
	CHECK("reading broke no controller rule", fake_nvmc.violations == 0u);
}

/* ---- writes ------------------------------------------------------------- */

static void test_write(void)
{
	static const uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint8_t out[8];

	reset_all();

	CHECK("a write succeeds", woz_freertos_flash_write(KV_BASE, data, sizeof(data)) == 0);
	CHECK("a write lands in flash", memcmp(&fake_nvmc_flash[KV_BASE], data, sizeof(data)) == 0);
	CHECK("a write programs one word at a time", fake_nvmc.word_writes == 2u);
	CHECK("a write broke no controller rule", fake_nvmc.violations == 0u);

	/*
	 * The controller must be left in read mode. Leaving it in write mode
	 * turns any later stray store into a flash program.
	 */
	CHECK("the controller is left read-only", fake_nvmc.mode == NRF_NVMC_MODE_READONLY);

	CHECK("a write reads back", woz_freertos_flash_read(KV_BASE, out, sizeof(out)) == 0 &&
					    memcmp(out, data, sizeof(data)) == 0);

	/* Clearing further bits is legal; setting one is not, and the model knows. */
	{
		static const uint8_t clear_more[4] = { 0, 0, 0, 0 };

		CHECK("clearing more bits succeeds",
		      woz_freertos_flash_write(KV_BASE, clear_more, sizeof(clear_more)) == 0);
		CHECK("clearing more bits broke no rule", fake_nvmc.violations == 0u);
	}
}

static void test_write_rejections(void)
{
	static const uint8_t data[8] = { 0 };

	reset_all();

	CHECK("an unaligned write offset is refused",
	      woz_freertos_flash_write(KV_BASE + 1u, data, 4) != 0);
	CHECK("a write length that is not whole words is refused",
	      woz_freertos_flash_write(KV_BASE, data, 3) != 0);
	CHECK("a null write buffer is refused", woz_freertos_flash_write(KV_BASE, NULL, 4) != 0);
	CHECK("a zero-length write succeeds", woz_freertos_flash_write(KV_BASE, data, 0) == 0);

	/*
	 * The window is what stops a store with an offset bug erasing the
	 * firmware out from under a door lock.
	 */
	CHECK("a write below the writable window is refused",
	      woz_freertos_flash_write(KV_BASE - 4u, data, 4) != 0);
	CHECK("a write past the writable window is refused",
	      woz_freertos_flash_write(KV_LIMIT - 4u, data, 8) != 0);
	CHECK("a write into the application image is refused",
	      woz_freertos_flash_write(0x8000u, data, 4) != 0);

	CHECK("no rejected write reached the controller", fake_nvmc.word_writes == 0u);
	CHECK("no rejected write changed the mode", fake_nvmc.mode_changes == 0u);
	CHECK("no rejected write broke a rule", fake_nvmc.violations == 0u);
}

/* ---- erases ------------------------------------------------------------- */

static void test_erase(void)
{
	uint8_t out[4];

	reset_all();
	memset(&fake_nvmc_flash[KV_BASE], 0x00, PAGE);

	CHECK("an erase succeeds", woz_freertos_flash_erase(KV_BASE, PAGE) == 0);
	CHECK("an erase leaves ones", woz_freertos_flash_read(KV_BASE, out, sizeof(out)) == 0 &&
					     out[0] == 0xffu && out[3] == 0xffu);
	CHECK("an erase covers one page per call", fake_nvmc.page_erases == 1u);
	CHECK("the controller is left read-only after an erase",
	      fake_nvmc.mode == NRF_NVMC_MODE_READONLY);
	CHECK("an erase broke no controller rule", fake_nvmc.violations == 0u);

	/* Both pages at once, which is what a compaction asks for. */
	reset_all();
	CHECK("a two-page erase succeeds", woz_freertos_flash_erase(KV_BASE, 2u * PAGE) == 0);
	CHECK("a two-page erase erases both", fake_nvmc.page_erases == 2u);
}

static void test_erase_rejections(void)
{
	reset_all();

	CHECK("an unaligned erase offset is refused",
	      woz_freertos_flash_erase(KV_BASE + 4u, PAGE) != 0);
	CHECK("an erase length that is not whole pages is refused",
	      woz_freertos_flash_erase(KV_BASE, PAGE + 4u) != 0);
	CHECK("a zero-length erase succeeds", woz_freertos_flash_erase(KV_BASE, 0) == 0);
	CHECK("an erase below the writable window is refused",
	      woz_freertos_flash_erase(KV_BASE - PAGE, PAGE) != 0);
	CHECK("an erase past the writable window is refused",
	      woz_freertos_flash_erase(KV_BASE, 3u * PAGE) != 0);
	CHECK("an erase of the application image is refused",
	      woz_freertos_flash_erase(0x8000u, PAGE) != 0);

	CHECK("no rejected erase reached the controller", fake_nvmc.page_erases == 0u);
	CHECK("no rejected erase changed the mode", fake_nvmc.mode_changes == 0u);
}

/* ---- arbitration -------------------------------------------------------- */

/*
 * Programming this flash stalls the CPU for far longer than a radio event can
 * wait, so a write issued while the radio is up needs an MPSL timeslot. That
 * binding is not written, and what matters is that its absence refuses rather
 * than proceeding: a failed provisioning write is recoverable, a corrupted
 * radio event on a shipping lock is not.
 */
static void test_radio_arbitration(void)
{
	static const uint8_t data[4] = { 1, 2, 3, 4 };

	reset_all();
	g_radio_ready = true;

	CHECK("a write while the radio is up is refused",
	      woz_freertos_flash_write(KV_BASE, data, sizeof(data)) != 0);
	CHECK("an erase while the radio is up is refused",
	      woz_freertos_flash_erase(KV_BASE, PAGE) != 0);
	CHECK("a refused operation never reached the controller",
	      fake_nvmc.word_writes == 0u && fake_nvmc.page_erases == 0u);
	CHECK("a refused operation never changed the mode", fake_nvmc.mode_changes == 0u);
	CHECK("a refusal is logged as an error", g_error_logs >= 2u);

	/* Reads are unaffected: they do not touch the controller. */
	{
		uint8_t out[4];

		CHECK("a read while the radio is up still succeeds",
		      woz_freertos_flash_read(KV_BASE, out, sizeof(out)) == 0);
	}

	/* With the radio down there is nothing to arbitrate. */
	g_radio_ready = false;
	CHECK("a write with the radio down succeeds",
	      woz_freertos_flash_write(KV_BASE, data, sizeof(data)) == 0);
}

int main(void)
{
	test_read();
	test_write();
	test_write_rejections();
	test_erase();
	test_erase_rejections();
	test_radio_arbitration();

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
