/*
 * The DW3110's reset, interrupt and wake lines, against register-level GPIO and
 * GPIOTE models.
 *
 * The GPIOTE model does not raise an event unless the channel would: bound to
 * the pin, enabled, and the transition matching the configured polarity. That
 * is the failure this port is most exposed to, because a driver that enables
 * the interrupt without binding the channel produces a board where ranging
 * simply never begins, with nothing in the log to say why.
 *
 * The interrupt path is checked end to end without a scheduler: the test moves
 * the pin, asks the model whether the part would have interrupted, calls the
 * vector if so, and then runs the worker task's body itself. What that proves
 * is the division of labour -- that the vector only notifies, and that the
 * draining loop belongs to the task -- which is the property the ~1.836 ms
 * response-arm deadline actually rests on.
 *
 * Each scenario forks, because the backend's task handle and interrupt flag are
 * static and a fresh peripheral model underneath them would leave the port
 * believing it had configured hardware that is now blank.
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fake_freertos.h>
#include <fake_nrf.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#include <hal/nrf_spim.h>
#include <nrfx.h>

#include <ultrawidelock_freertos_board.h>
#include <ultrawidelock_freertos_platform.h>
#include <ultrawidelock_freertos_uwb.h>

void GPIOTE_IRQHandler(void);

#include "board_pins.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"

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

/* ---- doubles ------------------------------------------------------------ */

static unsigned g_isr_calls;
static unsigned g_errors_logged;
static uint64_t g_busy_wait_total_us;
static bool g_idle_rc;

/*
 * The decadriver's own entry points. dwt_isr is the reason the vector cannot do
 * the work itself: on the board it reads the chip over SPI and then runs the
 * ranging callbacks. Here it counts, and drops the line after enough passes so
 * the draining loop is observably a loop and observably terminates.
 */
static unsigned g_isr_passes_before_line_drops = 1;

void dwt_isr(void)
{
	g_isr_calls++;
	if (g_isr_calls >= g_isr_passes_before_line_drops) {
		fake_gpio_input_set(ULTRAWIDELOCK_DW3000_PIN_IRQ, false);
	}
}

int dwt_checkidlerc(void)
{
	return g_idle_rc ? 1 : 0;
}

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag, const char *fmt, ...)
{
	(void)tag;
	(void)fmt;
	if (level == ULTRAWIDELOCK_FREERTOS_LOG_ERROR) {
		g_errors_logged++;
	}
}

void ultrawidelock_freertos_busy_wait_us(uint64_t us)
{
	g_busy_wait_total_us += us;
}

uint32_t ultrawidelock_freertos_cycle_get_32(void)
{
	static uint32_t tick;

	return ++tick;
}

/* ---- helpers ------------------------------------------------------------ */

static void bring_up(void)
{
	fake_freertos_reset();
	fake_gpio_reset();
	fake_gpiote_reset();
	fake_spim_reset();
	fake_nrf_reset();
	g_idle_rc = true;

	CHECK("the hardware layer comes up", dw3000_hw_init() == 0);
}

/*
 * One trip through the interrupt path, with no scheduler under it: raise the
 * line, run the vector if the part would have taken one, then run what the
 * worker task runs once it is notified.
 *
 * Through GPIOTE_IRQHandler rather than the driver's handler, because the
 * vector is shared: board/gpiote_freertos.c owns it and fans it out to whoever
 * registered. Calling the driver directly would pass with the registration
 * missing, which is precisely the state this port shipped in before the
 * dispatcher existed -- the edge reached default_handler and spun.
 */
static bool deliver_irq(void)
{
	bool interrupted;

	fake_gpiote_pin_edge(ULTRAWIDELOCK_DW3000_PIN_IRQ, true);
	interrupted = fake_gpiote_would_interrupt();
	if (interrupted) {
		GPIOTE_IRQHandler();
	}
	return interrupted;
}

/*
 * Run the port's own worker body until it blocks for the next notification.
 *
 * Not a copy of the loop: the whole question here is whether the backend drains
 * the line or services one event, and a test that reimplements the drain would
 * answer that question about itself. The kernel double calls this hook when the
 * task blocks, which is where one pass ends.
 */
static jmp_buf g_worker_blocked;

static void worker_blocked(void)
{
	longjmp(g_worker_blocked, 1);
}

static void pump_worker(void)
{
	fake_notify_block_hook = worker_blocked;
	if (setjmp(g_worker_blocked) == 0) {
		fake_task_entry(fake_task_arg);
	}
	fake_notify_block_hook = NULL;
}

/* ---- checks ------------------------------------------------------------- */

static void check_init(void)
{
	bring_up();

	/*
	 * Reset is open drain with the module's pull-up on it, so released
	 * means an input. A port that drove it high instead would fight the
	 * chip every time the chip resets itself.
	 */
	CHECK("the reset line comes up released, not driven high",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_RST].configured &&
		      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_RST].dir == NRF_GPIO_PIN_DIR_INPUT);
	/*
	 * No wake line on this board, and asserting its absence rather than its
	 * state is the point: the number the SDK gives for it is P1.19, which
	 * the nRF52833 does not have. See ports/.../uwb/board_pins.h.
	 */
	CHECK("bring-up touches no pin the nRF52833 does not have",
	      fake_gpio_absent_pin_touches == 0u);
	CHECK("the SPI bus came up with it", fake_spim.enabled);
}

static void check_interrupt_setup(void)
{
	bring_up();
	CHECK("the interrupt comes up", dw3000_hw_init_interrupt() == 0);

	CHECK("the interrupt line is an input",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_IRQ].configured &&
		      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_IRQ].dir == NRF_GPIO_PIN_DIR_INPUT);
	/*
	 * Bound to the pin, not merely enabled. An unbound channel with its
	 * interrupt unmasked is a board where nothing ever interrupts, and the
	 * symptom is ranging that never starts rather than an error anywhere.
	 */
	CHECK("a GPIOTE channel is bound to the interrupt pin",
	      fake_gpiote[0].configured && fake_gpiote[0].pin == ULTRAWIDELOCK_DW3000_PIN_IRQ);
	CHECK("the channel is enabled", fake_gpiote[0].enabled);
	CHECK("the channel watches for the chip asserting the line",
	      fake_gpiote[0].polarity == NRF_GPIOTE_POLARITY_LOTOHI);
	CHECK("the GPIOTE interrupt is unmasked",
	      nrf_gpiote_int_enable_check(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK) != 0u);
	CHECK("the port reports the interrupt enabled", dw3000_hw_interrupt_is_enabled());

	/*
	 * Four, and the reason is not tidiness. At five the MPSL low-priority
	 * handler on SWI5_EGU5 preempts this line, and the deadline between the
	 * chip raising an event and the response being armed is under two
	 * milliseconds. Four is also the most urgent level at which the handler
	 * may still notify a task.
	 */
	CHECK("the vector sits at the priority peripherals.yml froze",
	      fake_nvic_get_priority(GPIOTE_IRQn) == 4u);
	CHECK("the vector is enabled in the NVIC", fake_nvic_get_enable_irq(GPIOTE_IRQn) != 0u);

	CHECK("a worker task was created", fake_task_count == 1u);
	/*
	 * dwt_isr does SPI and then runs the ranging callbacks, so the work
	 * cannot happen in the handler; the task is what makes the handler a
	 * notify. Its priority is the isolation, since there is one core.
	 */
	CHECK("the worker runs above everything else on the part",
	      fake_task_priority == configMAX_PRIORITIES - 1);

	CHECK("bringing the interrupt up twice creates no second worker",
	      dw3000_hw_init_interrupt() == 0 && fake_task_count == 1u);
}

static void check_interrupt_delivery(void)
{
	bring_up();
	(void)dw3000_hw_init_interrupt();

	g_isr_passes_before_line_drops = 3;
	g_isr_calls = 0;

	CHECK("the chip asserting the line interrupts the part", deliver_irq());
	/*
	 * The vector must not do the work. On the board, calling dwt_isr here
	 * would run SPI and the ranging callbacks at priority 4, above the
	 * scheduler and beside MPSL's own housekeeping.
	 */
	CHECK("the vector runs no driver work of its own", g_isr_calls == 0u);
	CHECK("the vector notified the worker", fake_isr_notify_calls == 1u);
	CHECK("the vector asked for a reschedule on the way out", fake_isr_yield_calls == 1u);

	/*
	 * GPIOTE latches. A vector that returns without clearing re-enters
	 * immediately and the part livelocks at that priority, which no amount
	 * of later correctness recovers from.
	 */
	CHECK("the vector cleared the latched event", !fake_gpiote[0].event);

	pump_worker();
	/*
	 * The line stays high until every pending event has been read out, so
	 * one edge can owe several passes. A worker that called dwt_isr once
	 * would leave events unread and the line still asserted -- and the next
	 * rising edge never comes, because it never fell.
	 */
	CHECK("the worker drained the line rather than servicing one event",
	      g_isr_calls == 3u);
	CHECK("the line is released once it is drained",
	      nrf_gpio_pin_read(ULTRAWIDELOCK_DW3000_PIN_IRQ) == 0u);
}

static void check_spurious_vector(void)
{
	bring_up();
	(void)dw3000_hw_init_interrupt();

	/* No event behind it: the vector must notify nobody. Through the shared
	 * vector, because a handler that ignores the event check would see
	 * every other channel's edges once the update button takes one. */
	GPIOTE_IRQHandler();
	CHECK("a vector with no event behind it notifies nothing",
	      fake_isr_notify_calls == 0u);
	CHECK("a vector with no event behind it yields nothing", fake_isr_yield_calls == 0u);
}

/*
 * The routing itself, asked of the dispatcher rather than of the driver.
 *
 * Registering twice is what an image that re-initialises the interrupt does,
 * and a table that took the handler twice would call dwt_isr twice per edge --
 * which reads downstream as a duplicated frame, not as a registration bug.
 */
static void check_vector_registration(void)
{
	bring_up();
	CHECK("the DW3110 line is registered on the shared GPIOTE vector",
	      dw3000_hw_init_interrupt() == 0);
	CHECK("registering the same handler again is accepted",
	      dw3000_hw_init_interrupt() == 0);

	fake_isr_notify_calls = 0u;
	fake_gpiote_pin_edge(ULTRAWIDELOCK_DW3000_PIN_IRQ, true);
	GPIOTE_IRQHandler();
	CHECK("one edge notifies the worker exactly once", fake_isr_notify_calls == 1u);
}

static void check_interrupt_masking(void)
{
	bring_up();
	(void)dw3000_hw_init_interrupt();

	/*
	 * This is where the decadriver's decamutexon and decamutexoff land. It
	 * masks the chip's own line and nothing else: the radio, the tick and
	 * MPSL keep running, because this is a driver critical section rather
	 * than a global one.
	 */
	dw3000_hw_interrupt_disable();
	CHECK("masking clears the GPIOTE interrupt",
	      nrf_gpiote_int_enable_check(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK) == 0u);
	CHECK("masking is reported", !dw3000_hw_interrupt_is_enabled());
	CHECK("nothing else was masked with it",
	      fake_nvic_get_enable_irq(GPIOTE_IRQn) != 0u);

	fake_gpiote_pin_edge(ULTRAWIDELOCK_DW3000_PIN_IRQ, true);
	CHECK("a masked line raises no interrupt", !fake_gpiote_would_interrupt());
	/*
	 * But the event still latches, which is why unmasking has to be enough
	 * to deliver it: the chip does not assert twice.
	 */
	CHECK("the event still latched while masked", fake_gpiote[0].event);

	dw3000_hw_interrupt_enable();
	CHECK("unmasking is reported", dw3000_hw_interrupt_is_enabled());
	CHECK("the event held while masked is delivered on unmasking",
	      fake_gpiote_would_interrupt());
}

static void check_reset(void)
{
	bring_up();
	dw3000_hw_mark_asleep();
	g_busy_wait_total_us = 0;

	dw3000_hw_reset();

	/*
	 * Asserted by driving low and released by going back to an input, not
	 * by driving high: the line is open drain with the module's pull-up.
	 */
	CHECK("reset ends with the line released, not driven",
	      fake_gpio[ULTRAWIDELOCK_DW3000_PIN_RST].dir == NRF_GPIO_PIN_DIR_INPUT);
	CHECK("reset drove the line low while it was asserted",
	      !fake_gpio[ULTRAWIDELOCK_DW3000_PIN_RST].level);
	/*
	 * The chip needs time to climb from INIT_RC to IDLE_RC, and a port that
	 * carries on early talks to a chip that answers SPI with nonsense.
	 */
	CHECK("reset waited for the chip to reach IDLE_RC", g_busy_wait_total_us >= 2000u);
	CHECK("reset clears the sleep state", !dw3000_hw_is_asleep());
}

static void check_wakeup(void)
{
	bring_up();
	g_busy_wait_total_us = 0;
	g_errors_logged = 0;

	/* Awake already: waking is a no-op, and must not strobe chip select. */
	dw3000_hw_wakeup();
	CHECK("waking an awake chip does nothing", g_busy_wait_total_us == 0u);

	dw3000_hw_mark_asleep();
	CHECK("the chip reports itself asleep", dw3000_hw_is_asleep());
	g_idle_rc = true;
	g_busy_wait_total_us = 0;
	dw3000_hw_wakeup();
	CHECK("waking clears the sleep state", !dw3000_hw_is_asleep());
	CHECK("waking pulsed chip select", fake_gpio[ULTRAWIDELOCK_DW3000_PIN_CS].writes >= 2u);
	/*
	 * 500 us for the chip-select pulse plus 2000 for the climb from INIT_RC
	 * to IDLE_RC. The second is the one that matters: a DW3110 still in
	 * INIT_RC answers SPI, so talking to it early does not fail, it returns
	 * numbers from a radio that has not finished starting.
	 */
	CHECK("waking held the pulse and then waited for the chip to start",
	      g_busy_wait_total_us >= 2500u);
	CHECK("waking reported no failure", g_errors_logged == 0u);
}

static void check_wakeup_that_fails(void)
{
	bring_up();
	dw3000_hw_mark_asleep();
	g_errors_logged = 0;

	/*
	 * A DW3110 still in INIT_RC answers SPI and returns nonsense, so a port
	 * that assumes the wake worked produces a ranging session with plausible
	 * numbers from a radio that was never configured. The check is the only
	 * thing between that and a silent wrong answer.
	 */
	g_idle_rc = false;
	dw3000_hw_wakeup();
	CHECK("a chip that never reaches IDLE_RC is reported", g_errors_logged == 1u);
}

static void check_wakeup_pin(void)
{
	bring_up();
	dw3000_hw_wakeup_pin_low();
	/*
	 * The call is a no-op here and must stay harmless: dw3000_hw.h is shared
	 * with the ESP32 port, which does have the pin, so the entry point
	 * cannot be deleted -- only emptied.
	 */
	CHECK("lowering an absent wake pin touches no pin the part lacks",
	      fake_gpio_absent_pin_touches == 0u);
}

static void check_fini(void)
{
	bring_up();
	(void)dw3000_hw_init_interrupt();

	dw3000_hw_fini();
	CHECK("shutting down masks the interrupt",
	      nrf_gpiote_int_enable_check(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK) == 0u);
	CHECK("shutting down releases the GPIOTE channel", !fake_gpiote[0].enabled);
	CHECK("shutting down takes the SPI bus down too", !fake_spim.enabled);

	fake_gpiote_pin_edge(ULTRAWIDELOCK_DW3000_PIN_IRQ, true);
	CHECK("a released channel raises nothing", !fake_gpiote_would_interrupt());
}

/* ---- harness ------------------------------------------------------------ */

static void (*const g_scenarios[])(void) = {
	check_init,
	check_interrupt_setup,
	check_interrupt_delivery,
	check_spurious_vector,
	check_vector_registration,
	check_interrupt_masking,
	check_reset,
	check_wakeup,
	check_wakeup_that_fails,
	check_wakeup_pin,
	check_fini,
};

#define SCENARIO_COUNT ((int)(sizeof(g_scenarios) / sizeof(g_scenarios[0])))

static bool run_child(int scenario)
{
	pid_t pid;
	int status = 0;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		g_scenarios[scenario]();
		printf("RESULT-PART: %u checks\n", g_checks);
		fflush(stdout);
		_exit(g_failures == 0 ? 0 : 1);
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

	printf("dw3000-hw: %s (%d scenarios)\n", failures == 0 ? "PASS" : "FAIL", SCENARIO_COUNT);
	return failures == 0 ? 0 : 1;
}
