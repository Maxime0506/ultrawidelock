/*
 * OpenThread's entropy, reset, and assertion platform. The reset reason is the
 * part worth testing: RESETREAS accumulates bits across resets, so a reader
 * that does not clear it keeps reporting the first reason the part ever had,
 * and more than one bit can be set at once.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_nrf.h"

#include <hal/nrf_power.h>
#include <nrfx.h>
#include <woz_freertos_platform.h>
#include <openthread/platform/entropy.h>
#include <openthread/platform/misc.h>

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

/* The platform entropy source, under the test's control. */
static bool g_entropy_fails;
static unsigned g_entropy_calls;
static size_t g_entropy_length;

int woz_freertos_entropy(void *buffer, size_t length)
{
	g_entropy_calls++;
	g_entropy_length = length;
	if (g_entropy_fails) {
		return -1;
	}
	memset(buffer, 0xa5, length);
	return 0;
}

void woz_freertos_log(enum woz_freertos_log_level level, const char *tag, const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

_Noreturn void woz_freertos_fatal(const char *reason)
{
	printf("RESULT: FAIL (fatal: %s)\n", reason);
	_Exit(1);
}

int main(void)
{
	uint8_t buffer[8];

	fake_nrf_reset();
	fake_primask_reset();
	fake_power_reset();

	memset(buffer, 0, sizeof(buffer));
	CHECK("entropy fills the caller's buffer from the platform source",
	      otPlatEntropyGet(buffer, sizeof(buffer)) == OT_ERROR_NONE &&
		      g_entropy_length == sizeof(buffer) && buffer[0] == 0xa5 &&
		      buffer[sizeof(buffer) - 1] == 0xa5);
	CHECK("a null buffer or a zero length is rejected without asking the source",
	      otPlatEntropyGet(NULL, sizeof(buffer)) == OT_ERROR_INVALID_ARGS &&
		      otPlatEntropyGet(buffer, 0) == OT_ERROR_INVALID_ARGS &&
		      g_entropy_calls == 1);

	g_entropy_fails = true;
	CHECK("a source failure is reported rather than passed off as entropy",
	      otPlatEntropyGet(buffer, sizeof(buffer)) == OT_ERROR_FAILED);
	g_entropy_fails = false;

	/*
	 * A watchdog reset that followed a lockup sets both bits. The more
	 * specific reason is the one the stack should report.
	 */
	fake_power.resetreas = NRF_POWER_RESETREAS_DOG_MASK | NRF_POWER_RESETREAS_LOCKUP_MASK;
	CHECK("the most specific reset reason wins when several bits are set",
	      otPlatGetResetReason(NULL) == OT_PLAT_RESET_REASON_WATCHDOG);
	CHECK("and the register is cleared, or the next boot would inherit it",
	      fake_power.resetreas == 0 && fake_power.clear_calls == 1);

	/* Latched: a second call must not re-read a register it already cleared. */
	fake_power.resetreas = NRF_POWER_RESETREAS_RESETPIN_MASK;
	CHECK("the reason is latched at the first call, not re-read",
	      otPlatGetResetReason(NULL) == OT_PLAT_RESET_REASON_WATCHDOG &&
		      fake_power.clear_calls == 1);

	CHECK("a reset requests one from the core",
	      fake_system_reset_count() == 0 &&
		      (otPlatReset(NULL), fake_system_reset_count() == 1));
	CHECK("resetting into the bootloader is refused while there is no boot mode",
	      otPlatResetToBootloader(NULL) == OT_ERROR_NOT_CAPABLE &&
		      fake_system_reset_count() == 1);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
