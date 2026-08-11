/*
 * The board's three time hooks.
 *
 * They come from two sources on purpose, because the header asks two different
 * things of them. woz_freertos_uptime_us must never step backwards over the
 * life of the product, so it is built on the FreeRTOS tick count, which is
 * software-extended past its 32-bit wrap and therefore has no horizon at all.
 * woz_freertos_cycle_get_32 and woz_freertos_busy_wait_us need resolution finer
 * than a tick, so they read RTC1's counter directly.
 *
 * Reading RTC1 while the FreeRTOS port owns it is safe: an RTC counter free-runs
 * once started and nothing here writes a register. What this file does depend on
 * is the prescaler that port sets, which is stated below rather than probed --
 * the pinned Nordic HAL is not guaranteed to expose a prescaler read, and a
 * silent disagreement about the tick rate would show up as a busy wait of the
 * wrong length rather than as a build failure.
 *
 * The Zephyr oracle's k_cycle_get_32 on this part is the same 32768 Hz RTC, so
 * the DW3110 response-arm probe measures against the same resolution here as it
 * does there: 30.5 us against a 1.836 ms deadline.
 */
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_rtc.h>
#include <nrfx.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_platform.h>

/* RTC1 carries the FreeRTOS tick; peripherals.yml freezes that ownership. */
#define BOARD_RTC NRF_RTC1

/*
 * The frequency RTC1 counts at, which is 32768 Hz divided by the prescaler the
 * board's FreeRTOS tick sets. The board overrides this if it ever prescales.
 */
#ifndef WOZ_FREERTOS_BOARD_RTC_HZ
#define WOZ_FREERTOS_BOARD_RTC_HZ 32768u
#endif

/*
 * The largest span a single busy-wait pass may cover. The counter is 24 bits,
 * so a delta only reads unambiguously while it stays well inside that; a
 * megatick is three orders of magnitude of headroom and still 32 seconds of
 * work per pass.
 */
#define BUSY_WAIT_CHUNK_TICKS (1uL << 20)

/* How long to wait for the counter to prove it is running, in read attempts. */
#define CLOCK_CHECK_ATTEMPTS 1000000u

static bool s_clock_checked;
static uint32_t s_last_counter;
static uint32_t s_counter_high;
static uint32_t s_last_tick;
static uint64_t s_tick_high;

static bool in_isr(void)
{
	return __get_IPSR() != 0u;
}

static uint32_t counter_raw(void)
{
	return nrf_rtc_counter_get(BOARD_RTC) & NRF_RTC_COUNTER_MAX;
}

/*
 * Prove once that RTC1 is running before anything spins on it. A busy wait
 * against a stopped counter never returns, and a lock that hangs during driver
 * bring-up looks like dead hardware; failing loudly here names the cause.
 */
static void ensure_clock(void)
{
	uint32_t start;
	unsigned attempts;

	if (s_clock_checked) {
		return;
	}

	start = counter_raw();
	for (attempts = 0; attempts < CLOCK_CHECK_ATTEMPTS; attempts++) {
		if (counter_raw() != start) {
			s_clock_checked = true;
			return;
		}
	}
	woz_freertos_fatal("board RTC is not running; start the FreeRTOS tick first");
}

/*
 * The tick count, extended past its 32-bit wrap.
 *
 * The extension only needs a call per wrap, which is 49 days at a 1 kHz tick,
 * so nothing has to poll on its account. Interrupts are masked because the
 * count and the wrap total are read and written together and no ordering of
 * those two writes is safe to read lock-free.
 */
static uint64_t ticks_now(void)
{
	uint32_t primask = __get_PRIMASK();
	uint32_t now;
	uint64_t total;

	__disable_irq();
	now = in_isr() ? (uint32_t)xTaskGetTickCountFromISR() : (uint32_t)xTaskGetTickCount();
	if (now < s_last_tick) {
		s_tick_high += (uint64_t)1u << 32;
	}
	s_last_tick = now;
	total = s_tick_high + now;
	__set_PRIMASK(primask);
	return total;
}

int64_t woz_freertos_uptime_us(void)
{
	return (int64_t)((ticks_now() * 1000000u) / (uint64_t)configTICK_RATE_HZ);
}

/*
 * A free-running counter for latency probes.
 *
 * RTC1 counts 24 bits, so the top byte is carried in software. Callers only
 * ever difference two nearby reads, and a wrap between two such reads is caught
 * here; whole wraps that pass between one probe and the next are not, which
 * makes the absolute value meaningless and the differences correct. That is the
 * contract woz_port.h states for this hook.
 */
uint32_t woz_freertos_cycle_get_32(void)
{
	uint32_t primask = __get_PRIMASK();
	uint32_t now;
	uint32_t value;

	ensure_clock();

	__disable_irq();
	now = counter_raw();
	if (now < s_last_counter) {
		s_counter_high += (uint32_t)NRF_RTC_COUNTER_SPAN;
	}
	s_last_counter = now;
	value = s_counter_high + now;
	__set_PRIMASK(primask);
	return value;
}

/*
 * Spin for at least the requested time.
 *
 * At least, never less: the DW3000 driver asks for waits as short as 20 us and
 * this counter ticks every 30.5 us, so the request is rounded up and then given
 * one more tick, because the counter may be a hair from advancing when the spin
 * starts. Waiting too long costs microseconds during driver bring-up; waiting
 * too little violates a part's timing and fails intermittently on the bench.
 */
void woz_freertos_busy_wait_us(uint64_t us)
{
	uint64_t remaining;

	if (us == 0u) {
		return;
	}
	ensure_clock();

	remaining = ((us * WOZ_FREERTOS_BOARD_RTC_HZ) + 999999u) / 1000000u;
	remaining += 1u;

	while (remaining > 0u) {
		uint32_t chunk = remaining > BUSY_WAIT_CHUNK_TICKS ? (uint32_t)BUSY_WAIT_CHUNK_TICKS
								  : (uint32_t)remaining;
		uint32_t start = counter_raw();

		while (((counter_raw() - start) & (uint32_t)NRF_RTC_COUNTER_MAX) < chunk) {
			/* Spin. This hook is documented as not yielding. */
		}
		remaining -= chunk;
	}
}
