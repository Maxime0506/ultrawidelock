/*
 * The board's time and fault hooks.
 *
 * The RTC model free-runs the way the part does, so a busy wait terminates here
 * for the same reason it terminates on hardware, and setting its rate to zero
 * reproduces the stopped clock the port has to notice rather than spin on. The
 * fatal path is driven to its end through the reset model's hook, because on
 * hardware it never returns.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hal/nrf_rtc.h>
#include <nrfx.h>

#include <FreeRTOS.h>
#include <task.h>

#include <ultrawidelock_freertos_platform.h>

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

static char g_last_log[128];
static unsigned g_error_logs;

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	va_list args;

	(void)tag;
	if (level == ULTRAWIDELOCK_FREERTOS_LOG_ERROR) {
		g_error_logs++;
	}
	va_start(args, fmt);
	vsnprintf(g_last_log, sizeof(g_last_log), fmt, args);
	va_end(args);
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

/* The counter rate the port assumes, and the one the model runs at. */
#define RTC_HZ 32768u

static void reset_all(void)
{
	fake_rtc1_reset();
	fake_primask_reset();
	fake_ipsr_set(0);
	fake_task_set_tick_count(0);
	fake_system_reset_hook = NULL;
	g_error_logs = 0;
	g_last_log[0] = '\0';
	/* One counter tick per read: enough to make every wait terminate. */
	fake_rtc1_auto_advance = 1;
}

/* ---- uptime ------------------------------------------------------------- */

static void test_uptime(void)
{
	int64_t first;
	int64_t second;
	int64_t wrapped;

	reset_all();

	fake_task_set_tick_count(0);
	CHECK("uptime starts at zero", ultrawidelock_freertos_uptime_us() == 0);

	/* The model ticks at 1 kHz, so a tick is 1000 us. */
	fake_task_set_tick_count(5);
	CHECK("uptime converts ticks to microseconds", ultrawidelock_freertos_uptime_us() == 5000);

	fake_task_set_tick_count(1500);
	first = ultrawidelock_freertos_uptime_us();
	CHECK("uptime tracks the tick", first == 1500000);

	/*
	 * The wrap is the whole reason the count is extended. Park it one tick
	 * short of the end, cross it, and the result has to keep going up.
	 */
	fake_task_set_tick_count(0xffffffffu);
	second = ultrawidelock_freertos_uptime_us();
	CHECK("uptime reaches the end of the tick range", second > first);

	fake_task_set_tick_count(4);
	wrapped = ultrawidelock_freertos_uptime_us();
	CHECK("uptime does not step back across the tick wrap", wrapped > second);
	CHECK("uptime carries the wrap forward, not a restart", wrapped - second == 5000);

	/* An ISR reads the same clock through the other FreeRTOS entry point. */
	fake_ipsr_set(16);
	CHECK("uptime is readable from an interrupt", ultrawidelock_freertos_uptime_us() == wrapped);
	fake_ipsr_set(0);

	CHECK("uptime leaves interrupts as it found them", fake_primask_get() == 0);
}

/* ---- cycle counter ------------------------------------------------------ */

static void test_cycle_counter(void)
{
	uint32_t first;
	uint32_t second;
	uint32_t before_wrap;
	uint32_t after_wrap;

	reset_all();

	first = ultrawidelock_freertos_cycle_get_32();
	second = ultrawidelock_freertos_cycle_get_32();
	CHECK("the cycle counter advances", second > first);

	/*
	 * The counter is 24 bits and the hook reports 32, so a wrap between two
	 * nearby reads has to be carried. This is what a latency probe that
	 * straddles the wrap would otherwise get wrong by 16 million counts.
	 */
	fake_rtc1.counter = (uint32_t)NRF_RTC_COUNTER_MAX - 1u;
	before_wrap = ultrawidelock_freertos_cycle_get_32();
	after_wrap = ultrawidelock_freertos_cycle_get_32();
	CHECK("the cycle counter carries the 24-bit wrap", after_wrap > before_wrap);
	CHECK("the carry is one step, not a jump", after_wrap - before_wrap == 1u);

	CHECK("the cycle counter leaves interrupts as it found them", fake_primask_get() == 0);
	CHECK("the cycle counter masked interrupts while it read", fake_primask_disable_count() > 0);
}

/* ---- busy wait ---------------------------------------------------------- */

/* Counter ticks a wait of us microseconds actually consumed. */
static uint32_t wait_ticks(uint64_t us)
{
	uint32_t start = fake_rtc1.counter;

	ultrawidelock_freertos_busy_wait_us(us);
	return (fake_rtc1.counter - start) & (uint32_t)NRF_RTC_COUNTER_MAX;
}

static void test_busy_wait(void)
{
	uint32_t ticks;

	reset_all();

	CHECK("a zero wait reads no counter", wait_ticks(0) == 0u);

	/*
	 * These are exact counts, not floors, because the rounding rule is the
	 * thing worth pinning. A wait is the request rounded UP to whole ticks
	 * plus one guard tick, and the counts below are the only ones that
	 * satisfy both halves of that. A floor would pass just as happily on a
	 * wait that rounds down, which is the bug that matters: coming back
	 * early violates a part's timing and fails intermittently on a bench.
	 *
	 * One tick of the measured span is the read that primes the spin loop,
	 * so it is subtracted; the model advances the counter once per read.
	 */
	ticks = wait_ticks(20);
	CHECK("a sub-tick wait rounds up and adds the guard tick", ticks - 1u == 2u);
	CHECK("a sub-tick wait covers at least the request",
	      (uint64_t)(ticks - 1u) * 1000000u / RTC_HZ >= 20u);

	/* 1000 us is 32.768 ticks: 33 rounded up, 34 with the guard. */
	ticks = wait_ticks(1000);
	CHECK("a millisecond wait rounds up and adds the guard tick", ticks - 1u == 34u);
	CHECK("a millisecond wait does not overshoot wildly",
	      (uint64_t)(ticks - 1u) * 1000000u / RTC_HZ < 1000u + 100u);

	/* Longer than one pass of the chunked loop, to reach the second pass. */
	ticks = wait_ticks(40000000u);
	CHECK("a wait longer than one pass still completes",
	      (uint64_t)ticks * 1000000u / RTC_HZ >= 40000000u);
}

/* ---- a stopped clock ---------------------------------------------------- */

static jmp_buf g_fatal_jmp;

static void leave_fatal(void)
{
	longjmp(g_fatal_jmp, 1);
}

static void test_stopped_clock(void)
{
	unsigned resets_before;

	reset_all();
	/* A counter that never moves: the tick was never started. */
	fake_rtc1_auto_advance = 0;
	fake_system_reset_hook = leave_fatal;
	resets_before = fake_system_reset_count();

	if (setjmp(g_fatal_jmp) == 0) {
		(void)ultrawidelock_freertos_cycle_get_32();
		CHECK("a stopped clock is fatal rather than a spin", false);
	} else {
		CHECK("a stopped clock is fatal rather than a spin", true);
	}

	CHECK("the stopped clock was logged as an error", g_error_logs > 0);
	CHECK("the log names the cause", strstr(g_last_log, "RTC") != NULL);
	CHECK("the fatal path reset the part", fake_system_reset_count() == resets_before + 1u);
	CHECK("the fatal path masked interrupts before resetting",
	      fake_primask_disable_count() > 0);
	CHECK("the fatal path drained the write buffer first", fake_dsb_count() > 0);

	fake_primask_reset();
	fake_system_reset_hook = NULL;
}

int main(void)
{
	/*
	 * First, and not by preference: the port checks the clock once and
	 * remembers the answer, so any test that reads the counter ahead of this
	 * one would satisfy that check and leave the stopped case unreachable.
	 */
	test_stopped_clock();

	test_uptime();
	test_cycle_counter();
	test_busy_wait();

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
