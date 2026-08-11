/*
 * The RTC2 low-power timer platform. The service layer works in 64-bit
 * lpticks while RTC2 counts 24 bits, so most of what is checked here is the
 * extension across a wrap and the compare-already-passed races that would
 * otherwise cost a full 512 second revolution.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_nrf.h"

#include <hal/nrf_rtc.h>
#include <nrfx.h>
#include <platform/nrf_802154_platform_sl_lptimer.h>

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

static unsigned g_handler_calls;
static uint64_t g_handler_now;
static unsigned g_sync_calls;

void nrf_802154_sl_timer_handler(uint64_t now_lpticks)
{
	g_handler_calls++;
	g_handler_now = now_lpticks;
}

void nrf_802154_sl_timestamper_synchronized(void)
{
	g_sync_calls++;
}

/* Declared by the port for the board vector table. */
void nrf_802154_lptimer_freertos_irq_handler(void);

/* Runs the ISR the way the vector would, but only when the NVIC would let it. */
static void service_irq(void)
{
	if (!fake_nrf_irq_enabled[RTC2_IRQn]) {
		return;
	}
	fake_nrf_irq_pending[RTC2_IRQn] = false;
	nrf_802154_lptimer_freertos_irq_handler();
}

/* Advances the counter and lets the interrupt run, as it would on hardware. */
static void run_ticks(uint32_t ticks)
{
	uint32_t i;

	for (i = 0; i < ticks; i++) {
		fake_rtc_advance(1);
		if (fake_rtc2.event_overflow ||
		    (fake_rtc2.event_compare[0] &&
		     (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE0_MASK)) ||
		    (fake_rtc2.event_compare[1] &&
		     (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE1_MASK))) {
			service_irq();
		}
	}
}

static void reset_all(void)
{
	fake_rtc_reset();
	fake_nrf_reset();
	g_handler_calls = 0;
	g_handler_now = 0;
	g_sync_calls = 0;
}

int main(void)
{
	uint64_t now;
	uint64_t before;

	reset_all();

	nrf_802154_platform_sl_lp_timer_init();
	CHECK("init starts RTC2 at full resolution with the overflow interrupt on",
	      fake_rtc2.running && fake_rtc2.prescaler == 0 && fake_rtc2.clear_calls == 1 &&
		      (fake_rtc2.int_mask & NRF_RTC_INT_OVERFLOW_MASK) != 0);
	CHECK("init claims RTC2 at the priority peripherals.yml froze",
	      fake_nrf_irq_enabled[RTC2_IRQn] && fake_nrf_irq_priority[RTC2_IRQn] == 1);

	CHECK("a tick is reported as 31 microseconds, rounded up from 30.5",
	      nrf_802154_platform_sl_lptimer_granularity_get() == 31);

	/* 32768 ticks is exactly one second. */
	CHECK("microseconds convert to ticks at the crystal rate",
	      nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(1000000, false) == 32768 &&
		      nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(32768) == 1000000);
	CHECK("rounding up only adds a tick when the conversion is inexact",
	      nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(1000000, true) == 32768 &&
		      nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(31, false) == 1 &&
		      nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(31, true) == 2);

	CHECK("time starts at zero", nrf_802154_platform_sl_lptimer_current_lpticks_get() == 0);
	run_ticks(1000);
	CHECK("time follows the counter", nrf_802154_platform_sl_lptimer_current_lpticks_get() == 1000);

	/* The scheduled deadline must fire once, at or after the target. */
	nrf_802154_platform_sl_lptimer_schedule_at(1500);
	CHECK("scheduling ahead arms compare channel zero",
	      (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE0_MASK) != 0 &&
		      fake_rtc2.cc[0] == 1500 && g_handler_calls == 0);
	run_ticks(499);
	CHECK("the deadline does not fire early", g_handler_calls == 0);
	run_ticks(1);
	CHECK("the deadline fires exactly once, no earlier than the target",
	      g_handler_calls == 1 && g_handler_now >= 1500);
	CHECK("a fired timer disarms its compare interrupt",
	      (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE0_MASK) == 0);

	/* A target already in the past must not wait a whole wrap. */
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	nrf_802154_platform_sl_lptimer_schedule_at(now - 10);
	CHECK("a deadline in the past pends the timer interrupt instead of waiting",
	      fake_nrf_irq_pending[RTC2_IRQn]);
	service_irq();
	CHECK("the past deadline is delivered from the interrupt", g_handler_calls == 2);

	/* Same for a target too close for the compare write to be seen. */
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	nrf_802154_platform_sl_lptimer_schedule_at(now + 1);
	CHECK("a deadline inside the compare write window is not left to a wrap",
	      fake_nrf_irq_pending[RTC2_IRQn]);
	service_irq();
	CHECK("the too-close deadline is delivered too", g_handler_calls == 3);

	nrf_802154_platform_sl_lptimer_schedule_at(
		nrf_802154_platform_sl_lptimer_current_lpticks_get() + 100);
	nrf_802154_platform_sl_lptimer_disable();
	run_ticks(200);
	CHECK("a disabled timer does not fire",
	      g_handler_calls == 3 && (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE0_MASK) == 0);

	/* The critical section holds the handler off without losing it. */
	nrf_802154_platform_sl_lptimer_critical_section_enter();
	nrf_802154_platform_sl_lptimer_critical_section_enter();
	nrf_802154_platform_sl_lptimer_schedule_at(
		nrf_802154_platform_sl_lptimer_current_lpticks_get() + 50);
	run_ticks(100);
	CHECK("a critical section defers the handler", g_handler_calls == 3);
	nrf_802154_platform_sl_lptimer_critical_section_exit();
	CHECK("a nested critical section stays closed until the outer exit",
	      g_handler_calls == 3 && !fake_nrf_irq_enabled[RTC2_IRQn]);
	nrf_802154_platform_sl_lptimer_critical_section_exit();
	CHECK("leaving the critical section re-enables the vector",
	      fake_nrf_irq_enabled[RTC2_IRQn]);
	service_irq();
	CHECK("the deferred deadline is delivered, not dropped", g_handler_calls == 4);

	/* The 64-bit extension has to survive the 24-bit counter wrapping. */
	before = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	fake_rtc2.counter = NRF_RTC_COUNTER_MAX - 2u;
	run_ticks(6);
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	CHECK("time keeps increasing across a counter wrap",
	      now > before && now >= NRF_RTC_COUNTER_SPAN && now < NRF_RTC_COUNTER_SPAN + 16u);

	/*
	 * The dangerous wrap is the one the interrupt has not caught up with:
	 * the overflow event is set but the count still holds the old value. A
	 * critical section reproduces it exactly, and time must not go
	 * backwards while it is held.
	 */
	nrf_802154_platform_sl_lptimer_critical_section_enter();
	before = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	fake_rtc2.counter = NRF_RTC_COUNTER_MAX - 1u;
	fake_rtc_advance(4);
	CHECK("the overflow is pending with its interrupt held off",
	      fake_rtc2.event_overflow && fake_rtc2.counter < 4u);
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	CHECK("time does not go backwards across an unserviced wrap", now > before);
	nrf_802154_platform_sl_lptimer_critical_section_exit();
	service_irq();
	CHECK("servicing the deferred overflow does not double-count the wrap",
	      nrf_802154_platform_sl_lptimer_current_lpticks_get() == now);

	/* A deadline past the wrap still fires once, on the far side. */
	nrf_802154_platform_sl_lptimer_schedule_at(now + 100);
	run_ticks(100);
	CHECK("a deadline beyond the wrap fires once at the right time",
	      g_handler_calls == 5 && g_handler_now >= now + 100);

	/* The synchronization channel is separate and drives its own callback. */
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	nrf_802154_platform_sl_lptimer_sync_schedule_at(now + 60);
	CHECK("sync uses compare channel one, leaving the timer channel alone",
	      (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE1_MASK) != 0 &&
		      fake_rtc2.cc[1] == ((now + 60) & NRF_RTC_COUNTER_MAX) &&
		      nrf_802154_platform_sl_lptimer_sync_lpticks_get() == now + 60);
	run_ticks(60);
	CHECK("the sync callback runs once when its compare fires", g_sync_calls == 1);
	CHECK("a fired sync disarms its own interrupt",
	      (fake_rtc2.int_mask & NRF_RTC_INT_COMPARE1_MASK) == 0);

	nrf_802154_platform_sl_lptimer_sync_schedule_now();
	CHECK("sync scheduled now lands on a reachable tick, not in the past",
	      nrf_802154_platform_sl_lptimer_sync_lpticks_get() >
		      nrf_802154_platform_sl_lptimer_current_lpticks_get());
	run_ticks(10);
	CHECK("an immediate sync still delivers its callback", g_sync_calls == 2);

	nrf_802154_platform_sl_lptimer_sync_schedule_at(
		nrf_802154_platform_sl_lptimer_current_lpticks_get() + 50);
	nrf_802154_platform_sl_lptimer_sync_abort();
	run_ticks(100);
	CHECK("an aborted sync does not call back", g_sync_calls == 2);

	CHECK("the sync event address is a real RTC2 compare-one event register",
	      nrf_802154_platform_sl_lptimer_sync_event_get() ==
		      (uint32_t)(uintptr_t)&fake_rtc2 + (uint32_t)NRF_RTC_EVENT_COMPARE_1);

	/*
	 * The hardware-task binding needs PPI channels this port has not
	 * allocated. The contract has an answer for that, and refusing is what
	 * makes the service layer fall back instead of waiting for a trigger
	 * that never arrives.
	 */
	CHECK("the unwired hardware task reports no resources rather than accepting",
	      nrf_802154_platform_sl_lptimer_hw_task_prepare(1000, 3) ==
			      NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES &&
		      nrf_802154_platform_sl_lptimer_hw_task_update_ppi(3) ==
			      NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE &&
		      nrf_802154_platform_sl_lptimer_hw_task_cleanup() ==
			      NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE);

	nrf_802154_platform_sl_lp_timer_deinit();
	CHECK("deinit stops the counter and releases the vector",
	      !fake_rtc2.running && fake_rtc2.stop_calls == 1 &&
		      !fake_nrf_irq_enabled[RTC2_IRQn]);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
