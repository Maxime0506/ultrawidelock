/* SPDX-License-Identifier: ISC */

/*
 * OpenThread's entropy, reset, and assertion platform.
 *
 * All three are small, and all three matter more than their size suggests:
 * entropy feeds the stack's key material, and the reset reason is what the
 * stack reports when a joiner or a border router asks why the node restarted.
 *
 * The reset reason is latched once, at the first call. RESETREAS accumulates
 * bits across resets until something clears it, so reading it without clearing
 * would keep reporting the first reason this part ever had.
 */
#include <stdbool.h>
#include <stdint.h>

#include <ultrawidelock_freertos_platform.h>

#include <hal/nrf_power.h>
#include <nrfx.h>

#include <openthread/platform/entropy.h>
#include <openthread/platform/misc.h>

otError otPlatEntropyGet(uint8_t *output, uint16_t length)
{
	if (output == NULL || length == 0u) {
		return OT_ERROR_INVALID_ARGS;
	}
	if (ultrawidelock_freertos_entropy(output, length) != 0) {
		return OT_ERROR_FAILED;
	}
	return OT_ERROR_NONE;
}

void otPlatReset(otInstance *instance)
{
	(void)instance;
	NVIC_SystemReset();
}

/*
 * Resetting into the bootloader needs a boot-mode signal the DFU backend has
 * not defined yet. Reporting that the platform cannot do it is the contract's
 * own answer, and is honest; rebooting normally instead would look like a
 * successful bootloader entry to whatever asked.
 */
otError otPlatResetToBootloader(otInstance *instance)
{
	(void)instance;
	return OT_ERROR_NOT_CAPABLE;
}

static bool s_reason_latched;
static otPlatResetReason s_reason;

/*
 * More than one bit can be set: a watchdog reset that follows a lockup sets
 * both. They are read most-specific first, so the reason reported is the one
 * that says the most about what went wrong.
 */
static otPlatResetReason reason_from_mask(uint32_t mask)
{
	if ((mask & NRF_POWER_RESETREAS_DOG_MASK) != 0u) {
		return OT_PLAT_RESET_REASON_WATCHDOG;
	}
	if ((mask & NRF_POWER_RESETREAS_LOCKUP_MASK) != 0u) {
		return OT_PLAT_RESET_REASON_FAULT;
	}
	if ((mask & NRF_POWER_RESETREAS_SREQ_MASK) != 0u) {
		return OT_PLAT_RESET_REASON_SOFTWARE;
	}
	if ((mask & NRF_POWER_RESETREAS_RESETPIN_MASK) != 0u) {
		return OT_PLAT_RESET_REASON_EXTERNAL;
	}
	if ((mask & NRF_POWER_RESETREAS_OFF_MASK) != 0u) {
		/* Woken from System OFF, which the stack has no reason of its own for. */
		return OT_PLAT_RESET_REASON_OTHER;
	}
	/*
	 * No bit set at all is the one case the register states positively: a
	 * power-on reset clears it.
	 */
	if (mask == 0u) {
		return OT_PLAT_RESET_REASON_POWER_ON;
	}
	return OT_PLAT_RESET_REASON_OTHER;
}

otPlatResetReason otPlatGetResetReason(otInstance *instance)
{
	uint32_t mask;

	(void)instance;

	if (s_reason_latched) {
		return s_reason;
	}

	mask = nrf_power_resetreas_get(NRF_POWER);
	nrf_power_resetreas_clear(NRF_POWER, mask);
	s_reason = reason_from_mask(mask);
	s_reason_latched = true;

	return s_reason;
}

void otPlatAssertFail(const char *filename, int line)
{
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, "openthread", "assert %s:%d",
				   filename, line);
	ultrawidelock_freertos_fatal("openthread assertion");
}
