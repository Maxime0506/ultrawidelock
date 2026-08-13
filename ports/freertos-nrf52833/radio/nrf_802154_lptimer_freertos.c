/*
 * nRF 802.15.4 low-power timer platform on RTC2.
 *
 * peripherals.yml freezes RTC2 to this driver at interrupt priority 1, above
 * the FreeRTOS tick on RTC1 and below MPSL. Qorvo's app_timer must not be
 * linked, because it claims the same peripheral.
 *
 * The service layer works in 64-bit "lpticks" while RTC2 counts 24 bits at
 * 32768 Hz, so the counter is extended here with an overflow count. Three of
 * the four compare channels are used: channel 0 for the scheduled timer,
 * channel 1 for the timestamper synchronization event, and channel 2 for the
 * hardware-task binding.
 *
 * Unlike most nRF peripherals the RTC only produces an event when its EVTEN
 * bit is set, so every channel this driver arms enables the event as well as
 * the interrupt.
 */
#include <platform/nrf_802154_platform_sl_lptimer.h>

#include <stdbool.h>
#include <stddef.h>

#include <hal/nrf_ppi.h>
#include <hal/nrf_rtc.h>
#include <nrfx.h>

#define LPTIMER_RTC NRF_RTC2
#define LPTIMER_IRQN RTC2_IRQn

/* peripherals.yml: RTC2 sits above the FreeRTOS tick and below MPSL. */
#define LPTIMER_IRQ_PRIORITY 1u

#define LPTIMER_CHANNEL_TIMER 0u
#define LPTIMER_CHANNEL_SYNC 1u
#define LPTIMER_CHANNEL_HW_TASK 2u

/* INTEN and EVTEN share bit positions, one bit per compare channel. */
#define LPTIMER_CHANNEL_MASK(ch) (1uL << (16u + (ch)))

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
 * Arms one compare channel at a raw counter value. The event is enabled for
 * every channel; the interrupt only for the ones this driver services, since
 * the hardware task is delivered by PPI and must not wake the CPU.
 */
static void compare_arm(uint32_t channel, uint32_t cc, bool enable_irq)
{
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get((uint8_t)channel));
	nrf_rtc_cc_set(LPTIMER_RTC, channel, cc);
	if (enable_irq) {
		nrf_rtc_int_enable(LPTIMER_RTC, LPTIMER_CHANNEL_MASK(channel));
	}
	nrf_rtc_event_enable(LPTIMER_RTC, LPTIMER_CHANNEL_MASK(channel));
}

static void compare_disarm(uint32_t channel)
{
	nrf_rtc_int_disable(LPTIMER_RTC, LPTIMER_CHANNEL_MASK(channel));
	nrf_rtc_event_disable(LPTIMER_RTC, LPTIMER_CHANNEL_MASK(channel));
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

	compare_arm(channel, (uint32_t)(deadline & LPTIMER_COUNTER_MASK), true);

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
	compare_disarm(LPTIMER_CHANNEL_TIMER);
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
	compare_disarm(LPTIMER_CHANNEL_SYNC);
}

void nrf_802154_platform_sl_lptimer_sync_abort(void)
{
	s_sync_armed = false;
	compare_disarm(LPTIMER_CHANNEL_SYNC);
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
 * peripheral's task over PPI, with no CPU involvement, so the compare is armed
 * with its event enabled but its interrupt left off. The channel itself is
 * allocated and enabled by the 802.15.4 driver core and handed in here, so all
 * this side owns is the event endpoint.
 *
 * The state machine is the contract's, not this port's: the service layer
 * prepares a binding, may re-point it at a different channel, and then cleans
 * it up, and each of those is only legal from one state. There is a single set
 * of peripherals, so a second preparation is refused rather than queued.
 */
enum hw_task_state {
	HW_TASK_IDLE = 0,
	HW_TASK_SETTING_UP,
	HW_TASK_READY,
	HW_TASK_UPDATING,
	HW_TASK_CLEANING,
};

static enum hw_task_state s_hw_task_state;
static uint32_t s_hw_task_ppi = NRF_802154_SL_HW_TASK_PPI_INVALID;
static uint64_t s_hw_task_fire_lpticks;

/*
 * Compare-and-set against every other context. The callers run at different
 * interrupt priorities, so masking this driver's own vector is not enough and
 * PRIMASK is held for the two instructions the swap takes.
 */
static bool hw_task_state_set(enum hw_task_state expected, enum hw_task_state next)
{
	uint32_t primask = __get_PRIMASK();
	bool swapped;

	__disable_irq();
	swapped = (s_hw_task_state == expected);
	if (swapped) {
		s_hw_task_state = next;
	}
	__set_PRIMASK(primask);

	return swapped;
}

static void hw_task_ppi_attach(uint32_t ppi_channel)
{
	if (ppi_channel == NRF_802154_SL_HW_TASK_PPI_INVALID) {
		return;
	}
	nrf_ppi_event_endpoint_setup(
		NRF_PPI, (nrf_ppi_channel_t)ppi_channel,
		nrf_rtc_event_address_get(LPTIMER_RTC,
					  nrf_rtc_compare_event_get(LPTIMER_CHANNEL_HW_TASK)));
}

static void hw_task_ppi_detach(void)
{
	if (s_hw_task_ppi != NRF_802154_SL_HW_TASK_PPI_INVALID) {
		nrf_ppi_event_endpoint_setup(NRF_PPI, (nrf_ppi_channel_t)s_hw_task_ppi, 0);
		s_hw_task_ppi = NRF_802154_SL_HW_TASK_PPI_INVALID;
	}
}

static void hw_task_abort(void)
{
	compare_disarm(LPTIMER_CHANNEL_HW_TASK);
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_HW_TASK));
	hw_task_ppi_detach();
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(
	uint64_t fire_lpticks, uint32_t ppi_channel)
{
	uint32_t primask;
	uint64_t now;

	if (!hw_task_state_set(HW_TASK_IDLE, HW_TASK_SETTING_UP)) {
		/* The one set of peripherals this platform has is already in use. */
		return NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES;
	}

	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();

	/*
	 * Past half a wrap the compare value stops being distinguishable from one
	 * already behind the counter. A software timer can hop toward a distant
	 * target, but a hardware task fires once and cannot, so it is refused.
	 */
	if (fire_lpticks > now && (fire_lpticks - now) > LPTIMER_MAX_HORIZON) {
		(void)hw_task_state_set(HW_TASK_SETTING_UP, HW_TASK_IDLE);
		return NRF_802154_SL_LPTIMER_PLATFORM_TOO_DISTANT;
	}

	primask = __get_PRIMASK();
	__disable_irq();

	now = nrf_802154_platform_sl_lptimer_current_lpticks_get();
	if (fire_lpticks <= now + LPTIMER_MIN_COMPARE_DISTANCE) {
		__set_PRIMASK(primask);
		(void)hw_task_state_set(HW_TASK_SETTING_UP, HW_TASK_IDLE);
		return NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE;
	}

	compare_arm(LPTIMER_CHANNEL_HW_TASK, (uint32_t)(fire_lpticks & LPTIMER_COUNTER_MASK),
		    false);
	hw_task_ppi_attach(ppi_channel);
	s_hw_task_ppi = ppi_channel;
	s_hw_task_fire_lpticks = fire_lpticks;

	/*
	 * The counter kept running while the compare was written. A match missed
	 * by a tick would not come round again for a full wrap, and unlike the
	 * software timer there is no interrupt here to notice, so the binding is
	 * torn back down instead.
	 */
	if (fire_lpticks <= nrf_802154_platform_sl_lptimer_current_lpticks_get()) {
		hw_task_abort();
		__set_PRIMASK(primask);
		(void)hw_task_state_set(HW_TASK_SETTING_UP, HW_TASK_IDLE);
		return NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE;
	}

	__set_PRIMASK(primask);

	(void)hw_task_state_set(HW_TASK_SETTING_UP, HW_TASK_READY);
	return NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void)
{
	if (!hw_task_state_set(HW_TASK_READY, HW_TASK_CLEANING)) {
		return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
	}

	hw_task_abort();

	(void)hw_task_state_set(HW_TASK_CLEANING, HW_TASK_IDLE);
	return NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(
	uint32_t ppi_channel)
{
	uint32_t primask;
	bool fired;

	if (!hw_task_state_set(HW_TASK_READY, HW_TASK_UPDATING)) {
		return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
	}

	primask = __get_PRIMASK();
	__disable_irq();

	/* The channel being replaced belongs to the caller again once it is
	 * dropped, so the stale endpoint is taken down before the new one goes
	 * up rather than left pointing at this compare.
	 */
	hw_task_ppi_detach();
	hw_task_ppi_attach(ppi_channel);
	s_hw_task_ppi = ppi_channel;

	/*
	 * The compare may have matched before the new channel was attached, in
	 * which case the task it should have triggered never ran. Report that
	 * rather than leave the caller waiting on an event that already went by.
	 */
	fired = nrf_rtc_event_check(LPTIMER_RTC,
				    nrf_rtc_compare_event_get(LPTIMER_CHANNEL_HW_TASK)) ||
		(nrf_802154_platform_sl_lptimer_current_lpticks_get() >= s_hw_task_fire_lpticks);

	__set_PRIMASK(primask);

	(void)hw_task_state_set(HW_TASK_UPDATING, HW_TASK_READY);
	return fired ? NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE
		     : NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
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
	s_hw_task_state = HW_TASK_IDLE;
	s_hw_task_ppi = NRF_802154_SL_HW_TASK_PPI_INVALID;
	s_hw_task_fire_lpticks = 0;

	/* Prescaler zero keeps the full 32768 Hz resolution. */
	nrf_rtc_prescaler_set(LPTIMER_RTC, 0);
	nrf_rtc_event_clear(LPTIMER_RTC, NRF_RTC_EVENT_OVERFLOW);
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_TIMER));
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_SYNC));
	nrf_rtc_event_clear(LPTIMER_RTC, nrf_rtc_compare_event_get(LPTIMER_CHANNEL_HW_TASK));

	/*
	 * The overflow interrupt is what keeps the 64-bit extension honest, and
	 * the RTC only raises the event at all once EVTEN says so.
	 */
	nrf_rtc_int_enable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK);
	nrf_rtc_event_enable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK);

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
	hw_task_abort();
	s_hw_task_state = HW_TASK_IDLE;
	nrf_rtc_int_disable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK | NRF_RTC_INT_COMPARE0_MASK |
						NRF_RTC_INT_COMPARE1_MASK);
	nrf_rtc_event_disable(LPTIMER_RTC, NRF_RTC_INT_OVERFLOW_MASK | NRF_RTC_INT_COMPARE0_MASK |
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
/*
 * The RTC2 vector itself.
 *
 * Defined here rather than in board/startup_freertos.c because that file cannot
 * reach this symbol -- the board layer does not link ultrawidelock_802154 -- and because
 * this is the file peripherals.yml names as the owner. Startup keeps a weak
 * alias so an image built without the 802.15.4 layer still links; this strong
 * definition wins whenever the layer is present, since the driver's ordinary
 * calls into this file already pull the object.
 *
 * Without it the weak alias resolved to default_handler and the first low-power
 * timer interrupt would have parked the core in a spin loop.
 * scripts/freertos-vector-check.sh now fails the build on that condition.
 */
void nrf_802154_lptimer_freertos_irq_handler(void);

void RTC2_IRQHandler(void)
{
	nrf_802154_lptimer_freertos_irq_handler();
}

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
			compare_disarm(LPTIMER_CHANNEL_SYNC);
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
	compare_disarm(LPTIMER_CHANNEL_TIMER);
	nrf_802154_sl_timer_handler(nrf_802154_platform_sl_lptimer_current_lpticks_get());
}
