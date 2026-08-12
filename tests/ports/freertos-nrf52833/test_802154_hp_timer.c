/*
 * The high-precision timer platform is Nordic's own file, compiled from the
 * vendor tree unmodified because it already depends on nothing but nrfx and the
 * Nordic HAL. What is under test is that decision: that it builds and behaves
 * against this port's frozen TIMER1 assignment without a compatibility layer.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <hal/nrf_timer.h>
#include <platform/nrf_802154_hp_timer.h>

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

int main(void)
{
	uint32_t timestamp = 0;
	uint32_t first;
	uint32_t second;

	fake_timer_reset();

	nrf_802154_hp_timer_init();
	CHECK("the high-precision timer counts microseconds at full width",
	      fake_timer1.bit_width == NRF_TIMER_BIT_WIDTH_32 &&
		      fake_timer1.prescaler == NRF_TIMER_FREQ_1MHz &&
		      fake_timer1.mode == NRF_TIMER_MODE_TIMER);

	nrf_802154_hp_timer_start();
	CHECK("starting runs the timer", fake_timer1.running && fake_timer1.start_calls == 1);

	first = nrf_802154_hp_timer_current_time_get();
	fake_timer_advance(1000);
	second = nrf_802154_hp_timer_current_time_get();
	CHECK("current time advances one count per microsecond", second - first == 1000);

	/*
	 * Synchronization works by parking a value in the compare channel that
	 * the capture cannot produce, then noticing when it changes.
	 */
	nrf_802154_hp_timer_sync_prepare();
	CHECK("an unfired synchronization reports nothing",
	      !nrf_802154_hp_timer_sync_time_get(&timestamp));

	fake_timer_advance(500);
	/* The LP timer's event drives this capture over PPI on hardware. */
	fake_timer_capture(2);
	CHECK("a fired synchronization reports the captured timestamp",
	      nrf_802154_hp_timer_sync_time_get(&timestamp) &&
		      timestamp == nrf_802154_hp_timer_current_time_get());

	nrf_802154_hp_timer_sync_prepare();
	CHECK("preparing again re-arms the detection",
	      !nrf_802154_hp_timer_sync_time_get(&timestamp));

	/* Event timestamping uses its own channel and must not disturb sync. */
	fake_timer_advance(250);
	fake_timer_capture(3);
	CHECK("an event timestamp is captured independently of synchronization",
	      nrf_802154_hp_timer_timestamp_get() == nrf_802154_hp_timer_current_time_get() &&
		      !nrf_802154_hp_timer_sync_time_get(&timestamp));

	CHECK("the synchronization and timestamp tasks are different TIMER1 registers",
	      nrf_802154_hp_timer_sync_task_get() != nrf_802154_hp_timer_timestamp_task_get() &&
		      nrf_802154_hp_timer_sync_task_get() ==
			      (uint32_t)(uintptr_t)&fake_timer1 +
				      (uint32_t)NRF_TIMER_TASK_CAPTURE2 &&
		      nrf_802154_hp_timer_timestamp_task_get() ==
			      (uint32_t)(uintptr_t)&fake_timer1 +
				      (uint32_t)NRF_TIMER_TASK_CAPTURE3);

	nrf_802154_hp_timer_stop();
	CHECK("stopping shuts the timer down", !fake_timer1.running);

	nrf_802154_hp_timer_deinit();
	CHECK("deinit shuts it down too", fake_timer1.shutdown_calls == 2);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
