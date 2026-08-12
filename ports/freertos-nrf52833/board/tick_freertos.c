/*
 * The FreeRTOS tick, on RTC1.
 *
 * This file replaces the pinned kernel's own tick file. That file,
 * portable/CMSIS/nrf52/port_cmsis_systick.c, is unusable here for two separate
 * reasons, and it is worth naming both because either alone would be enough:
 *
 *  - It calls nrf_drv_clock_lfclk_request(). MPSL owns CLOCK on this build
 *    (peripherals.yml) and starts the low-frequency clock itself inside
 *    mpsl_init(). A second requester writing those registers behind MPSL's back
 *    is the kind of conflict that shows up much later as a drifting radio
 *    timebase.
 *
 *  - It prescales RTC1 so the counter itself ticks at configTICK_RATE_HZ. That
 *    would leave board/time_freertos.c reading a 1 kHz counter while it states
 *    32768 Hz, making every woz_freertos_busy_wait_us about 33 times too long
 *    and coarsening the DW3110 response-arm probe from 30.5 us to 1 ms.
 *
 * So RTC1 runs unprescaled at 32768 Hz and the tick comes from COMPARE0 stepped
 * 32 counts at a time, which is exactly 1024 Hz. The counter stays the
 * full-rate free-running timebase that time_freertos.c documents, and the
 * kernel gets its tick from a compare on top of it.
 *
 * Ticks are counted from the counter rather than one per interrupt. A tick
 * interrupt at priority 7 can be held off for a long time by the radio, and
 * deriving the elapsed count from the hardware means a late interrupt catches
 * up instead of losing time. The same property is why the pinned kernel's
 * version has its own auto-correction loop.
 *
 * Ordering requirement: LFCLK must already be running when the scheduler
 * starts, so mpsl_init() has to run before vTaskStartScheduler(). The
 * application's startup does that, and a stopped counter is caught here rather
 * than left to hang.
 */
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_rtc.h>
#include <nrfx.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_platform.h>

#define TICK_RTC NRF_RTC1
#define TICK_RTC_IRQn RTC1_IRQn

/* The unprescaled crystal rate. board/time_freertos.c states the same number. */
#define TICK_RTC_HZ 32768u

/*
 * Counts per tick. The static assertion is the whole reason 1024 Hz was chosen
 * over 1000: an inexact division here would accumulate against the radio's
 * timebase rather than fail the build.
 */
#define TICK_COUNTS (TICK_RTC_HZ / configTICK_RATE_HZ)
_Static_assert(TICK_RTC_HZ % configTICK_RATE_HZ == 0u,
	       "configTICK_RATE_HZ must divide 32768 exactly");
_Static_assert(TICK_COUNTS >= 2u, "a tick must be at least two RTC counts");

#define TICK_CC_CHANNEL 0u

/*
 * EVTEN and INTEN use the same bit positions, so one mask serves both. The
 * pinned HAL has no separate EVTEN mask macro; reaching for one would be a
 * macro that exists only in the host fake.
 */
#define TICK_COMPARE_MASK NRF_RTC_INT_COMPARE0_MASK

/* How long to wait for the counter to prove it is running, in read attempts. */
#define TICK_CLOCK_CHECK_ATTEMPTS 1000000u

/*
 * The counter value the last delivered tick was aligned to. Written only by the
 * handler and by setup, both with the compare interrupt not yet enabled or from
 * within it, so it needs no lock.
 */
static uint32_t s_tick_anchor;

static uint32_t counter_get(void)
{
	return nrf_rtc_counter_get(TICK_RTC) & (uint32_t)NRF_RTC_COUNTER_MAX;
}

static void arm_next_compare(void)
{
	nrf_rtc_cc_set(TICK_RTC, TICK_CC_CHANNEL, (s_tick_anchor + TICK_COUNTS) & NRF_RTC_COUNTER_MAX);
}

/*
 * Prove LFCLK is running before the scheduler depends on it. A tick that never
 * arrives looks like a hung board; naming the cause here costs one bounded loop
 * at startup and saves an afternoon on the bench.
 */
static void ensure_running(void)
{
	uint32_t start = counter_get();
	unsigned attempts;

	for (attempts = 0; attempts < TICK_CLOCK_CHECK_ATTEMPTS; attempts++) {
		if (counter_get() != start) {
			return;
		}
	}
	woz_freertos_fatal("RTC1 is not counting; start MPSL before the scheduler");
}

/*
 * Called by the kernel from xPortStartScheduler(). The vendor port declares
 * this and provides no definition once its own tick file is left out, which is
 * exactly the seam this port is meant to fill.
 */
void vPortSetupTimerInterrupt(void)
{
	nrf_rtc_task_trigger(TICK_RTC, NRF_RTC_TASK_STOP);
	nrf_rtc_prescaler_set(TICK_RTC, 0u);
	nrf_rtc_task_trigger(TICK_RTC, NRF_RTC_TASK_CLEAR);

	s_tick_anchor = 0u;
	arm_next_compare();

	/*
	 * The RTC raises nothing until EVTEN is set, whatever INTEN says, so
	 * both are enabled. The same trap is documented in the low-power timer.
	 */
	nrf_rtc_event_clear(TICK_RTC, NRF_RTC_EVENT_COMPARE_0);
	nrf_rtc_event_enable(TICK_RTC, TICK_COMPARE_MASK);
	nrf_rtc_int_enable(TICK_RTC, TICK_COMPARE_MASK);

	NVIC_SetPriority(TICK_RTC_IRQn, configKERNEL_INTERRUPT_PRIORITY);
	NVIC_ClearPendingIRQ(TICK_RTC_IRQn);
	NVIC_EnableIRQ(TICK_RTC_IRQn);

	nrf_rtc_task_trigger(TICK_RTC, NRF_RTC_TASK_START);
	ensure_running();
}

void RTC1_IRQHandler(void)
{
	BaseType_t switch_required = pdFALSE;
	uint32_t mask;
	uint32_t elapsed;
	uint32_t ticks;

	nrf_rtc_event_clear(TICK_RTC, NRF_RTC_EVENT_COMPARE_0);

	mask = portSET_INTERRUPT_MASK_FROM_ISR();

	elapsed = (counter_get() - s_tick_anchor) & (uint32_t)NRF_RTC_COUNTER_MAX;
	ticks = elapsed / TICK_COUNTS;

	/*
	 * A spurious wake with nothing due still has to leave a compare armed,
	 * or this is the last tick the system ever sees.
	 */
	if (ticks != 0u) {
		s_tick_anchor = (s_tick_anchor + (ticks * TICK_COUNTS)) & NRF_RTC_COUNTER_MAX;

		/*
		 * Deliver every tick that passed. The scheduler being suspended
		 * caps this at one, because xTaskIncrementTick reports the state
		 * from when the suspension began and repeating it would double
		 * count; this mirrors the pinned kernel's own correction.
		 */
		if (ticks > 1u && xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
			ticks = 1u;
		}
		while (ticks-- > 0u) {
			switch_required |= xTaskIncrementTick();
		}
	}

	arm_next_compare();

	/*
	 * Re-check after arming. If those counts went by while the compare was
	 * being written, the event is already latched and this handler runs
	 * again; if the write landed early enough, nothing is lost either way.
	 */
	if (((counter_get() - s_tick_anchor) & (uint32_t)NRF_RTC_COUNTER_MAX) >= TICK_COUNTS) {
		NVIC_SetPendingIRQ(TICK_RTC_IRQn);
	}

	if (switch_required != pdFALSE) {
		SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
		__SEV();
	}

	portCLEAR_INTERRUPT_MASK_FROM_ISR(mask);
}
