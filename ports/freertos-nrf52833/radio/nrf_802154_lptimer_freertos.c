/*
 * nRF 802.15.4 low-power timer platform on RTC2.
 *
 * peripherals.yml freezes RTC2 to this driver at interrupt priority 1, above
 * the FreeRTOS tick on RTC1 and below MPSL. Qorvo's app_timer must not be
 * linked, because it claims the same peripheral.
 *
 * The service layer works in 64-bit "lpticks" while RTC2 counts 24 bits at
 * 32768 Hz, so the counter is extended here with an overflow count. Two of the
 * four compare channels are used: channel 0 for the scheduled timer and
 * channel 1 for the timestamper synchronization event. Channel 2 is reserved
 * for the hardware-task binding, which is not wired yet.
 */
#include <platform/nrf_802154_platform_sl_lptimer.h>

#include <stdbool.h>
#include <stddef.h>

#include <hal/nrf_rtc.h>
#include <nrfx.h>

#define LPTIMER_RTC NRF_RTC2
#define LPTIMER_IRQN RTC2_IRQn

/* peripherals.yml: RTC2 sits above the FreeRTOS tick and below MPSL. */
#define LPTIMER_IRQ_PRIORITY 1u

#define LPTIMER_CHANNEL_TIMER 0u
#define LPTIMER_CHANNEL_SYNC 1u

/* The RTC counter is 24 bits and its LFCLK is exactly 32768 Hz. */
#define LPTIMER_COUNTER_BITS 24u
#define LPTIMER_COUNTER_SPAN (1uLL << LPTIMER_COUNTER_BITS)
#define LPTIMER_COUNTER_MASK (LPTIMER_COUNTER_SPAN - 1uLL)
#define LPTIMER_FREQUENCY 32768uLL
#define LPTIMER_US_PER_SECOND 1000000uLL

/*
 * A compare only fires on an exact match, so a value written too close to the
 * current counter is missed and would not come round again for a full 512
 * second wrap. The nRF52 RTC needs the compare to be at least two ticks ahead,
 * so anything nearer than that is treated as already elapsed.
 */
#define LPTIMER_MIN_COMPARE_DISTANCE 3uLL

/*
 * The nearest target a compare can actually be programmed for. The minimum
 * distance is the last value that gets rejected, so a reachable one is a tick
 * beyond it.
 */
#define LPTIMER_NEAREST_OFFSET (LPTIMER_MIN_COMPARE_DISTANCE + 1uLL)

/* Bounded retries for the sync channel when the counter moves under us. */
#define LPTIMER_SYNC_ATTEMPTS 8u

/*
 * Never program a compare more than half a wrap ahead: past that the target
 * stops being distinguishable from one already in the past. Longer waits are
 * broken into hops, each rescheduled from the interrupt.
 */
#define LPTIMER_MAX_HORIZON (LPTIMER_COUNTER_SPAN / 2uLL)

static uint32_t s_overflow_count;
static uint64_t s_target_lpticks;
static uint64_t s_sync_lpticks;
static bool s_timer_armed;
static bool s_sync_armed;
static uint32_t s_critical_section_depth;
static bool s_initialized;

static uint64_t counter_extend(uint32_t overflows, uint32_t counter)
{
	return ((uint64_t)overflows << LPTIMER_COUNTER_BITS) | (uint64_t)counter;
}

/*
 * The counter and the overflow count are two halves of one value maintained by
 * two different contexts, and the interrupt updates them one after the other.
 * There is no ordering of those two writes that a lock-free reader can always
 * interpret correctly: whichever comes first, a reader landing between them
 * sees a value that is either a wrap short or a wrap long. Masking the timer
 * interrupt for the handful of cycles the read takes removes the window
 * outright.
 *
 * This is re-entrant. Inside the driver's own critical section the vector is
 * already masked, so both branches are skipped; inside the timer interrupt
 * masking has no effect on the handler already running.
 */
uint64_t nrf_802154_platform_sl_lptimer_current_lpticks_get(void)
{
	bool unmasked = NVIC_GetEnableIRQ(LPTIMER_IRQN) != 0u;
	uint32_t overflows;
	uint32_t counter;

	if (unmasked) {
		NVIC_DisableIRQ(LPTIMER_IRQN);
	}

	counter = nrf_rtc_counter_get(LPTIMER_RTC);
	overflows = s_overflow_count;

	/*
	 * The overflow event can be set without its interrupt having run, either
	 * because it just fired or because a critical section is holding it off.
	 * The counter has already wrapped in that case, so the count has to be
	 * advanced here or time would appear to jump backwards.
	 */
	if (nrf_rtc_event_check(LPTIMER_RTC, NRF_RTC_EVENT_OVERFLOW) &&
	    counter < (LPTIMER_COUNTER_SPAN / 2uLL)) {
		overflows++;
	}

	if (unmasked) {
		NVIC_EnableIRQ(LPTIMER_IRQN);
	}

	return counter_extend(overflows, counter);
}

uint64_t nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(uint64_t us, bool round_up)
{
	uint64_t ticks = (us * LPTIMER_FREQUENCY) / LPTIMER_US_PER_SECOND;

	if (round_up && (ticks * LPTIMER_US_PER_SECOND) != (us * LPTIMER_FREQUENCY)) {
		ticks++;
	}
	return ticks;
}

uint64_t nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(uint64_t lpticks)
{
	return (lpticks * LPTIMER_US_PER_SECOND) / LPTIMER_FREQUENCY;
}

uint32_t nrf_802154_platform_sl_lptimer_granularity_get(void)
{
	/*
	 * One tick is 30.5 microseconds. The service layer uses this to round
	 * its own calculations, so reporting 30 would claim more resolution than
	 * a tick actually has; the value is rounded up.
	 */
	return (uint32_t)((LPTIMER_US_PER_SECOND + LPTIMER_FREQUENCY - 1uLL) / LPTIMER_FREQUENCY);
}

/*
 * Programs one compare channel toward an absolute target. Returns false when
 * the target is already reached, in which case the caller fires immediately
 * rather than waiting a full wrap for the counter to come back round.
 */
static bool compare_program(uint32_t channel, uint64_t target_lpticks)
{
	uint64_t now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	uint64_t distance;
	uint64_t deadline;

	if (target_lpticks <= now + LPTIMER_MIN_COMPARE_DISTANCE) {
		return false;
	}

	distance = target_lpticks - now;
	deadline = (distance > LPTIMER_MAX_HORIZON) ? (now + LPTIMER_MAX_HORIZON) : target_lpticks;

	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get((uint8_t)channel));
	nrf_rtc_cc_set(LPTIMER_RTC, channel, (uint32_t)(deadline & LPTIMER_COUNTER_MASK));
	nrf_rtc_int_enable(LPTIMER_RTC, channel == LPTIMER_CHANNEL_TIMER
					       ? NRF_RTC_INT_COMPARE0_MASK
					       : NRF_RTC_INT_COMPARE1_MASK);

	/*
	 * The counter kept running while the compare was written, so re-check
	 * that the deadline has not just gone past. Losing this race would cost
	 * a full wrap.
	 */
	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	if (deadline <= now) {
		return false;
	}
	return true;
}

static void timer_fire_now(void)
{
	/*
	 * The contract requires the handler to run from the timer's own
	 * interrupt even when the deadline has already passed, and requires a
	 * critical section to hold it off. Pending the interrupt satisfies both.
	 */
	NVIC_SetPendingIRQ(LPTIMER_IRQN);
}

void nrf_802154_platform_sl_lptimer_schedule_at(uint64_t fire_lpticks)
{
	s_target_lpticks = fire_lpticks;
	s_timer_armed = true;

	if (!compare_program(LPTIMER_CHANNEL_TIMER, fire_lpticks)) {
		timer_fire_now();
	}
}

void nrf_802154_platform_sl_lptimer_disable(void)
{
	s_timer_armed = false;
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_COMPARE0_MASK);
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_TIMER));
}

void nrf_802154_platform_sl_lptimer_critical_section_enter(void)
{
	s_critical_section_depth++;
	if (s_critical_section_depth == 1u) {
		NVIC_DisableIRQ(LPTIMER_IRQN);
	}
}

void nrf_802154_platform_sl_lptimer_critical_section_exit(void)
{
	if (s_critical_section_depth == 0u) {
		return;
	}
	s_critical_section_depth--;
	if (s_critical_section_depth == 0u) {
		/*
		 * Anything that came due inside the section stays pending in the
		 * NVIC and runs as soon as the vector is enabled again.
		 */
		NVIC_EnableIRQ(LPTIMER_IRQN);
	}
}

void nrf_802154_platform_sl_lptimer_sync_schedule_now(void)
{
	nrf_802154_platform_sl_lptimer_sync_schedule_at(
		nrf_802154_platform_sl_lptimer_current_lpticks_get() + LPTIMER_NEAREST_OFFSET);
}

void nrf_802154_platform_sl_lptimer_sync_schedule_at(uint64_t fire_lpticks)
{
	uint64_t offset = LPTIMER_NEAREST_OFFSET;
	unsigned attempt;

	s_sync_lpticks = fire_lpticks;
	s_sync_armed = true;

	if (compare_program(LPTIMER_CHANNEL_SYNC, fire_lpticks)) {
		return;
	}

	/*
	 * Unlike the scheduled timer, this cannot be delivered by pending an
	 * interrupt: the synchronization event is a hardware event that other
	 * peripherals subscribe to, so it has to come from a real compare match.
	 * Re-aim at the nearest reachable tick, backing off if the counter keeps
	 * moving past the target while it is being programmed.
	 */
	for (attempt = 0; attempt < LPTIMER_SYNC_ATTEMPTS; attempt++) {
		s_sync_lpticks =
			nrf_802154_platform_sl_lptimer_current_lpticks_get() + offset;
		if (compare_program(LPTIMER_CHANNEL_SYNC, s_sync_lpticks)) {
			return;
		}
		offset *= 2uLL;
	}

	/*
	 * Eight doublings could not outrun the counter, which means the clock is
	 * not behaving. Leave the channel disarmed rather than claim a
	 * synchronization point that will never arrive.
	 */
	s_sync_armed = false;
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_COMPARE1_MASK);
}

void nrf_802154_platform_sl_lptimer_sync_abort(void)
{
	s_sync_armed = false;
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_COMPARE1_MASK);
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC));
}

uint32_t nrf_802154_platform_sl_lptimer_sync_event_get(void)
{
	return nrf_rtc_event_address_get(LPTIMER_RTC,
					 nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC));
}

uint64_t nrf_802154_platform_sl_lptimer_sync_lpticks_get(void)
{
	return s_sync_lpticks;
}

/*
 * The hardware-task binding routes a compare event straight to another
 * peripheral's task over PPI, which this port has not allocated channels for
 * yet. Reporting that there are no resources is the contract's own answer for
 * exactly this case, and the service layer falls back to software triggering,
 * so it is refused here rather than silently accepted and never delivered.
 */
nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(
	uint64_t fire_lpticks, uint32_t ppi_channel)
{
	(void)fire_lpticks;
	(void)ppi_channel;
	return NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void)
{
	return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(
	uint32_t ppi_channel)
{
	(void)ppi_channel;
	return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

void nrf_802154_platform_sl_lp_timer_init(void)
{
	if (s_initialized) {
		return;
	}

	s_overflow_count = 0;
	s_target_lpticks = 0;
	s_sync_lpticks = 0;
	s_timer_armed = false;
	s_sync_armed = false;
	s_critical_section_depth = 0;

	/* Prescaler zero keeps the full 32768 Hz resolution. */
	nrf_rtc_prescaler_set(LPTIMER_RTC, 0);
	nrf_rtc_event_clear(LPTIMER_RTC, NRF_RTC_EVENT_OVERFLOW);
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_TIMER));
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC));

	/* The overflow interrupt is what keeps the 64-bit extension honest. */
	nrf_rtc_int_enable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK);

	NVIC_SetPriority(LPTIMER_IRQN, LPTIMER_IRQ_PRIORITY);
	NVIC_ClearPendingIRQ(LPTIMER_IRQN);
	NVIC_EnableIRQ(LPTIMER_IRQN);

	nrf_rtc_task_trigger(LPTIMER_RTC, NRF_RTC_TASK_CLEAR);
	nrf_rtc_task_trigger(LPTIMER_RTC, NRF_RTC_TASK_START);

	s_initialized = true;
}

void nrf_802154_platform_sl_lp_timer_deinit(void)
{
	if (!s_initialized) {
		return;
	}
	NVIC_DisableIRQ(LPTIMER_IRQN);
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK | NRF_RTC_INT_COMPARE0_MASK |
						NRF_RTC_INT_COMPARE1_MASK);
	nrf_rtc_task_trigger(LPTIMER_RTC, NRF_RTC_TASK_STOP);
	s_timer_armed = false;
	s_sync_armed = false;
	s_initialized = false;
}

/*
 * Board vector entry for RTC2. Kept separate from the platform contract so the
 * board's vector table names one symbol per peripheral, as it does for MPSL.
 */
void nrf_802154_lptimer_freertos_irq_handler(void)
{
	if (nrf_rtc_event_check(LPTIMER_RTC, NRF_RTC_EVENT_OVERFLOW)) {
		nrf_rtc_event_clear(LPTIMER_RTC, NRF_RTC_EVENT_OVERFLOW);
		s_overflow_count++;
	}

	if (nrf_rtc_event_check(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC))) {
		nrf_rtc_event_clear(LPTIMER_RTC,
				    nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC));
		if (s_sync_armed) {
			s_sync_armed = false;
			nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_COMPARE1_MASK);
			nrf_802154_sl_timestamper_synchronized();
		}
	}

	if (nrf_rtc_event_check(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_TIMER))) {
		nrf_rtc_event_clear(LPTIMER_RTC,
				    nrf_rtc_compare_event_get(LPTIMER_CHANNEL_TIMER));
	}

	if (!s_timer_armed) {
		return;
	}

	/*
	 * A long wait is programmed in hops, so reaching the compare does not
	 * mean the deadline arrived. Only fire once the target is actually
	 * reached; otherwise aim at the next hop.
	 */
	if (nrf_802154_platform_sl_lptimer_current_lpticks_get() < s_target_lpticks) {
		if (compare_program(LPTIMER_CHANNEL_TIMER, s_target_lpticks)) {
			return;
		}
	}

	s_timer_armed = false;
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_COMPARE0_MASK);
	nrf_802154_sl_timer_handler(nrf_802154_platform_sl_lptimer_current_lpticks_get());
}
