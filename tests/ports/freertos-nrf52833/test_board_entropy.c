/*
 * The board's entropy and die-temperature hooks.
 *
 * The RNG model refuses to produce while the generator is stopped, which is the
 * rule that matters: a driver that waits on VALRDY without starting the
 * peripheral hangs on hardware and would pass against a model that just hands
 * over a number. Bytes reach the pool the way they do on the board, through the
 * interrupt entry point, and the direct-poll path is reached by asking for more
 * than the pool holds.
 *
 * Each scenario runs in its own process. The pool and the started flag are
 * static, so resetting the peripheral model underneath them would leave the
 * port believing it had started a generator that is now stopped -- and the
 * first direct poll would then spin forever against a model that is right.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <hal/nrf_rng.h>
#include <mpsl_temp.h>
#include <nrfx.h>

#include <ultrawidelock_freertos_board.h>
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

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	printf("  FAIL unexpected fatal: %s\n", reason);
	fflush(stdout);
	_Exit(1);
}

/* Deliver one byte the way the board does: the peripheral, then its vector. */
static void deliver(uint8_t value)
{
	fake_rng_produce(value);
	ultrawidelock_freertos_rng_isr();
}

/* Bring the pool up without leaving anything in it. */
static void start_empty(void)
{
	uint8_t byte = 0;

	fake_rng_auto_produce = 1;
	(void)ultrawidelock_freertos_entropy(&byte, 1);
	fake_rng_auto_produce = 0;
}

/* ---- scenarios ---------------------------------------------------------- */

static void scenario_start(void)
{
	uint8_t byte = 0;

	CHECK("the generator is idle before the first request", fake_rng.starts == 0u);

	fake_rng_auto_produce = 1;
	CHECK("the first request succeeds", ultrawidelock_freertos_entropy(&byte, 1) == 0);

	CHECK("bias correction is enabled", fake_rng.error_correction);
	CHECK("the value-ready interrupt is enabled", fake_rng.int_valrdy);
	CHECK("the generator was started", fake_rng.starts > 0u);
	CHECK("no byte was taken from a stopped generator", fake_rng.violations == 0u);
}

static void scenario_pool(void)
{
	uint8_t out[4];

	start_empty();

	deliver(0x11);
	deliver(0x22);
	deliver(0x33);
	deliver(0x44);

	memset(out, 0, sizeof(out));
	CHECK("a pooled request succeeds", ultrawidelock_freertos_entropy(out, 4) == 0);
	CHECK("the pool answers in order",
	      out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33 && out[3] == 0x44);
	CHECK("a pooled request never polls the peripheral", fake_rng.violations == 0u);

	/*
	 * A vector with no event behind it must take nothing. Reading VALUE
	 * anyway would put the last byte into the pool a second time, so the
	 * only way to see the bug is to ask for a byte afterwards and check it
	 * is not that stale one.
	 */
	ultrawidelock_freertos_rng_isr();
	fake_rng_auto_produce = 1;
	memset(out, 0, sizeof(out));
	CHECK("a request after a spurious vector succeeds", ultrawidelock_freertos_entropy(out, 1) == 0);
	CHECK("a vector with no event pooled nothing", out[0] != 0x44);
}

static void scenario_full_pool(void)
{
	uint8_t out[32];
	unsigned i;

	start_empty();

	/* One past the pool's depth, to reach the refusal. */
	for (i = 0; i < 33u; i++) {
		deliver((uint8_t)i);
	}
	CHECK("the generator stops once the pool is full", !fake_rng.running);
	CHECK("a full pool leaves the extra byte in the peripheral",
	      fake_rng.violations == 1u);

	/*
	 * A byte already generated when the generator is told to stop still
	 * raises its event, and the vector still runs. The handler has to drop
	 * that byte: putting it into a full ring overwrites the oldest entry and
	 * leaves the count past the pool's depth, so every later request reads
	 * from the wrong place. This is the only way to reach that guard, since
	 * a stopped generator otherwise produces nothing.
	 */
	nrf_rng_task_trigger(NRF_RNG, NRF_RNG_TASK_START);
	deliver(0xee);
	nrf_rng_task_trigger(NRF_RNG, NRF_RNG_TASK_STOP);

	CHECK("a request against a full pool succeeds", ultrawidelock_freertos_entropy(out, 32) == 0);
	CHECK("the pool answered the whole request in order", out[0] == 0u && out[31] == 31u);
	CHECK("draining the pool restarts the generator", fake_rng.running);
}

static void scenario_direct_poll(void)
{
	uint8_t out[8];

	/* The board's vector table has routed and enabled RNG by this point. */
	NVIC_EnableIRQ(RNG_IRQn);

	/* Nothing pooled, so every byte comes straight from the peripheral. */
	fake_rng_auto_produce = 8;

	CHECK("a request with an empty pool succeeds", ultrawidelock_freertos_entropy(out, 8) == 0);
	CHECK("every byte came from a running generator", fake_rng.violations == 0u);
	CHECK("the polled bytes are the ones produced", out[0] == 1u && out[7] == 8u);
	/*
	 * The mask is what stops the handler taking the byte the caller is
	 * spinning for. No host test can hit that race by accident, so the model
	 * watches the mask instead.
	 */
	CHECK("polling masked the RNG vector while it waited", fake_rng_masked_on_every_produce);
	CHECK("the RNG vector is left enabled after polling",
	      fake_nvic_get_enable_irq(RNG_IRQn) != 0u);
}

static void scenario_arguments(void)
{
	uint8_t byte = 0;

	fake_rng_auto_produce = 4;

	CHECK("a null buffer with a length is refused", ultrawidelock_freertos_entropy(NULL, 4) == -1);
	CHECK("a zero-length request succeeds", ultrawidelock_freertos_entropy(&byte, 0) == 0);
	CHECK("a zero-length request takes no byte", fake_rng_auto_produce == 4u);
}

static void scenario_temperature(void)
{
	/* MPSL answers quarter degrees; the consumer wants whole ones. */
	fake_mpsl_temperature_set(100);
	CHECK("a positive reading converts from quarter degrees",
	      ultrawidelock_freertos_die_temperature_c() == 25);

	fake_mpsl_temperature_set(-80);
	CHECK("a negative reading converts from quarter degrees",
	      ultrawidelock_freertos_die_temperature_c() == -20);

	fake_mpsl_temperature_set(103);
	CHECK("a fractional reading truncates toward zero",
	      ultrawidelock_freertos_die_temperature_c() == 25);

	fake_mpsl_temperature_set(0);
	CHECK("zero converts to zero", ultrawidelock_freertos_die_temperature_c() == 0);

	CHECK("every reading went to MPSL", fake_mpsl_temperature_reads() == 4u);
}

/* ---- driver ------------------------------------------------------------- */

enum scenario {
	SCENARIO_START,
	SCENARIO_POOL,
	SCENARIO_FULL_POOL,
	SCENARIO_DIRECT_POLL,
	SCENARIO_ARGUMENTS,
	SCENARIO_TEMPERATURE,
	SCENARIO_COUNT,
};

static int run_scenario(int scenario)
{
	fake_rng_reset();
	fake_mpsl_temperature_reset();

	switch (scenario) {
	case SCENARIO_START:
		scenario_start();
		break;
	case SCENARIO_POOL:
		scenario_pool();
		break;
	case SCENARIO_FULL_POOL:
		scenario_full_pool();
		break;
	case SCENARIO_DIRECT_POLL:
		scenario_direct_poll();
		break;
	case SCENARIO_ARGUMENTS:
		scenario_arguments();
		break;
	default:
		scenario_temperature();
		break;
	}
	printf("RESULT-PART: %u checks\n", g_checks);
	return g_failures == 0 ? 0 : 1;
}

static bool run_child(int scenario)
{
	pid_t pid;
	int status = 0;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int rc = run_scenario(scenario);

		fflush(stdout);
		_exit(rc);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		printf("  FAIL could not fork scenario %d\n", scenario);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	unsigned failures = 0;
	int scenario;

	for (scenario = 0; scenario < SCENARIO_COUNT; scenario++) {
		failures += run_child(scenario) ? 0u : 1u;
	}

	printf("RESULT: %s (%d scenarios)\n", failures == 0 ? "PASS" : "FAIL", SCENARIO_COUNT);
	return failures == 0 ? 0 : 1;
}
