/*
 * The nRF 802.15.4 clock platform. Every request has to go through MPSL,
 * because peripherals.yml gives MPSL the CLOCK peripheral, and it has to use
 * the source-selecting API because the pinned MPSL deprecates the older one.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_sdc.h"
#include "ultrawidelock_freertos_platform.h"

#include <platform/nrf_802154_clock.h>

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

static unsigned g_hfclk_ready_calls;
static unsigned g_lfclk_ready_calls;
static unsigned g_fatal_calls;

/* Supplied by the driver on target; recorded here. */
void nrf_802154_clock_hfclk_ready(void)
{
	g_hfclk_ready_calls++;
}

void nrf_802154_clock_lfclk_ready(void)
{
	g_lfclk_ready_calls++;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	g_fatal_calls++;
	printf("  FAIL unexpected fatal: %s\n", reason != NULL ? reason : "?");
	exit(1);
}

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

void ultrawidelock_freertos_log_hexdump(enum ultrawidelock_freertos_log_level level,
					const char *tag, const void *data, size_t len,
					const char *message)
{
	(void)level;
	(void)tag;
	(void)data;
	(void)len;
	(void)message;
}

int main(void)
{
	fake_sdc_reset();

	nrf_802154_clock_init();
	CHECK("clock init touches no hardware on nRF52833",
	      fake_mpsl_hfclk_request_calls == 0 && fake_mpsl_hfclk_release_calls == 0);

	CHECK("the high-frequency clock starts stopped", !nrf_802154_clock_hfclk_is_running());

	nrf_802154_clock_hfclk_start();
	CHECK("starting the crystal goes through MPSL for the one nRF52833 source",
	      fake_mpsl_hfclk_request_calls == 1 &&
		      fake_mpsl_hfclk_src == MPSL_CLOCK_HF_SRC_XO &&
		      fake_mpsl_hfclk_callback != NULL);
	CHECK("a requested crystal is not running until MPSL says so",
	      !nrf_802154_clock_hfclk_is_running() && g_hfclk_ready_calls == 0);

	nrf_802154_clock_hfclk_start();
	CHECK("a second start does not stack a second MPSL request",
	      fake_mpsl_hfclk_request_calls == 1);

	/* MPSL reuses one callback for several events; only one means started. */
	fake_mpsl_hfclk_callback(MPSL_CLOCK_EVT_XO_TUNED);
	CHECK("an unrelated clock event does not report readiness", g_hfclk_ready_calls == 0);

	fake_mpsl_hfclk_running = 1;
	fake_mpsl_hfclk_callback(MPSL_CLOCK_EVT_HFCLK_STARTED);
	CHECK("the started event reports readiness to the driver exactly once",
	      g_hfclk_ready_calls == 1);
	CHECK("a started crystal reports running", nrf_802154_clock_hfclk_is_running());

	fake_mpsl_hfclk_is_running_result = -1;
	CHECK("an MPSL query failure reads as not running",
	      !nrf_802154_clock_hfclk_is_running());
	fake_mpsl_hfclk_is_running_result = 0;

	nrf_802154_clock_hfclk_stop();
	CHECK("stopping releases the same source and reports not running",
	      fake_mpsl_hfclk_release_calls == 1 &&
		      fake_mpsl_hfclk_src == MPSL_CLOCK_HF_SRC_XO &&
		      !nrf_802154_clock_hfclk_is_running());

	nrf_802154_clock_hfclk_stop();
	CHECK("a second stop does not release a clock this port no longer holds",
	      fake_mpsl_hfclk_release_calls == 1);

	/*
	 * MPSL may still be holding the crystal for another protocol after our
	 * release, so a fresh request has to reach MPSL again rather than assume
	 * the previous grant survived.
	 */
	nrf_802154_clock_hfclk_start();
	CHECK("restarting after a stop issues a new MPSL request",
	      fake_mpsl_hfclk_request_calls == 2);

	/*
	 * mpsl_init() starts the low-frequency clock and waits for it, so by the
	 * time anything here runs it is already up.
	 */
	CHECK("the low-frequency clock is already running", nrf_802154_clock_lfclk_is_running());
	nrf_802154_clock_lfclk_start();
	CHECK("starting the low-frequency clock reports ready immediately",
	      g_lfclk_ready_calls == 1 && nrf_802154_clock_lfclk_is_running());

	nrf_802154_clock_lfclk_stop();
	CHECK("stopping the low-frequency clock is refused, it carries the tick",
	      nrf_802154_clock_lfclk_is_running());

	nrf_802154_clock_deinit();
	CHECK("deinit leaves the shared clock alone",
	      fake_mpsl_hfclk_release_calls == 1 && g_fatal_calls == 0);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
