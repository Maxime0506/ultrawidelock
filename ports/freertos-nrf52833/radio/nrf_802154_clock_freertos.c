/*
 * nRF 802.15.4 clock platform.
 *
 * peripherals.yml gives the CLOCK peripheral to MPSL, so nothing here touches
 * NRF_CLOCK directly; every request goes through the MPSL clock API that the
 * SoftDevice Controller shares.
 *
 * With the pinned MPSL-arbitrated service-layer binary, the driver only calls
 * init and deinit: HFCLK arbitration belongs to MPSL's own resource scheduler,
 * and libnrf-802154-sl.a leaves no clock symbol undefined. The remaining
 * entry points are implemented anyway because they are the header's contract
 * and the open-source scheduler variant does call them.
 */
#include <platform/nrf_802154_clock.h>

#include <stdbool.h>
#include <stddef.h>

#include "ultrawidelock_freertos_platform.h"

#include <mpsl_clock.h>

#define ULTRAWIDELOCK_FREERTOS_802154_CLOCK_TAG "802154_clk"

/*
 * The plain mpsl_clock_hfclk_request/release/is_running trio is deprecated in
 * the pinned MPSL and scheduled for removal, so this uses the _src_ variants
 * throughout. nRF52833 has exactly one high-frequency source, the crystal.
 */
#define ULTRAWIDELOCK_FREERTOS_802154_HF_SRC MPSL_CLOCK_HF_SRC_XO

static bool s_hfclk_requested;

void nrf_802154_clock_init(void)
{
	/*
	 * Nothing to do on nRF52833. The oracle only sets a startup latency here
	 * on the nRF54L series, and MPSL already owns the clock configuration
	 * this port committed to in radio/radio_start_freertos.c.
	 */
}

void nrf_802154_clock_deinit(void)
{
	/* Intentionally empty: the port never tears the radio down. */
}

/*
 * Runs in the mpsl_low_priority_process context, which is the shared MPSL
 * worker task, once the crystal has started. MPSL uses this one callback for
 * several event types, so the started event has to be selected explicitly.
 */
static void hfclk_event(mpsl_clock_evt_type_t evt_type)
{
	if (evt_type != MPSL_CLOCK_EVT_HFCLK_STARTED) {
		return;
	}
	nrf_802154_clock_hfclk_ready();
}

void nrf_802154_clock_hfclk_start(void)
{
	if (s_hfclk_requested) {
		return;
	}
	if (mpsl_clock_hfclk_src_request(ULTRAWIDELOCK_FREERTOS_802154_HF_SRC, hfclk_event) != 0) {
		ultrawidelock_freertos_fatal("802154 hfclk request");
	}
	s_hfclk_requested = true;
}

void nrf_802154_clock_hfclk_stop(void)
{
	if (!s_hfclk_requested) {
		return;
	}
	s_hfclk_requested = false;
	if (mpsl_clock_hfclk_src_release(ULTRAWIDELOCK_FREERTOS_802154_HF_SRC) != 0) {
		ultrawidelock_freertos_fatal("802154 hfclk release");
	}
}

bool nrf_802154_clock_hfclk_is_running(void)
{
	uint32_t running = 0;

	/*
	 * Asking MPSL rather than reporting the request flag: the request is
	 * granted asynchronously, and the driver uses this to decide whether the
	 * radio may key up. A release that MPSL has not acted on yet, because
	 * another protocol still holds the clock, must not read as stopped here.
	 */
	if (!s_hfclk_requested) {
		return false;
	}
	if (mpsl_clock_hfclk_src_is_running(ULTRAWIDELOCK_FREERTOS_802154_HF_SRC, &running) != 0) {
		return false;
	}
	return running != 0;
}

/*
 * The low-frequency clock is started by mpsl_init() before this port hands
 * control to anything else: radio/radio_start_freertos.c configures the
 * crystal source with skip_wait_lfclk_started false, so MPSL blocks until it
 * has actually started. There is therefore nothing to request and nothing to
 * wait for, and reporting readiness immediately is the truthful answer rather
 * than a shortcut.
 */
void nrf_802154_clock_lfclk_start(void)
{
	nrf_802154_clock_lfclk_ready();
}

void nrf_802154_clock_lfclk_stop(void)
{
	/*
	 * Stopping it would take the FreeRTOS tick and the SoftDevice Controller
	 * down with it, so this stays empty on purpose.
	 */
}

bool nrf_802154_clock_lfclk_is_running(void)
{
	return true;
}
