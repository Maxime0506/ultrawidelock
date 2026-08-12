/*
 * The Zephyr kernel objects the pinned OpenThread radio platform uses. What
 * matters here is not that a queue queues, but that every operation stays
 * correct when a driver callout runs it from an interrupt: the semaphore has
 * to pick the right FreeRTOS entry point, and the queue and the atomics have
 * to exclude one outright.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_freertos.h"
#include "fake_nrf.h"

#include <nrfx.h>
#include <ultrawidelock_freertos_platform.h>
#include <ultrawidelock_freertos_ot_kernel.h>

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

/* The platform's receive frame shape: a reserved link word, then payload. */
struct queued_item {
	void *reserved;
	unsigned id;
};

static unsigned g_fatal_calls;
static const char *g_fatal_reason;

_Noreturn void ultrawidelock_freertos_fatal(const char *reason)
{
	g_fatal_calls++;
	g_fatal_reason = reason;
	/* The real one stops the part; the test only needs it not to return. */
	printf("RESULT: FAIL (fatal: %s)\n", reason);
	_Exit(1);
}

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

int main(void)
{
	struct k_sem sem;
	struct k_fifo fifo;
	struct queued_item first = {NULL, 1};
	struct queued_item second = {NULL, 2};
	ATOMIC_DEFINE(events, 4);

	fake_freertos_reset();
	fake_nrf_reset();
	fake_primask_reset();
	memset(events, 0, sizeof(events));

	k_sem_init(&sem, 0, 1);
	CHECK("a semaphore starts empty at the limit it was given",
	      sem.handle != NULL && sem.handle->count == 0 && sem.handle->limit == 1);
	CHECK("taking an empty semaphore without waiting reports a timeout",
	      k_sem_take(&sem, K_NO_WAIT) != 0);

	k_sem_give(&sem);
	CHECK("a give from a task uses the task entry point",
	      sem.handle->count == 1 && fake_semaphore_isr_gives == 0);
	CHECK("the waiting side then takes it", k_sem_take(&sem, K_FOREVER) == 0);

	/* The driver callouts that signal this semaphore run in interrupt context. */
	fake_ipsr_set(16);
	k_sem_give(&sem);
	fake_ipsr_set(0);
	CHECK("a give from an interrupt uses the interrupt entry point",
	      sem.handle->count == 1 && fake_semaphore_isr_gives == 1);
	CHECK("and yields, so the waiting task runs at the exception return",
	      fake_isr_yield_calls == 1);
	CHECK("the value crosses the context boundary intact",
	      k_sem_take(&sem, K_NO_WAIT) == 0);

	k_fifo_init(&fifo);
	CHECK("an empty queue returns nothing rather than blocking",
	      k_fifo_get(&fifo, K_NO_WAIT) == NULL);

	k_fifo_put(&fifo, &first);
	k_fifo_put(&fifo, &second);
	CHECK("the queue is first in, first out",
	      k_fifo_get(&fifo, K_NO_WAIT) == &first && k_fifo_get(&fifo, K_NO_WAIT) == &second);
	CHECK("draining it empties it", k_fifo_get(&fifo, K_NO_WAIT) == NULL);

	/*
	 * The buffer pool is refilled from a driver callout and drained by the
	 * OpenThread task, so a push must not be interruptible partway.
	 */
	fake_primask_reset();
	k_fifo_put(&fifo, &first);
	CHECK("a push masks interrupts and restores the mask it found",
	      fake_primask_disable_count() == 1 && fake_primask_get() == 0);
	CHECK("so does a pop",
	      k_fifo_get(&fifo, K_NO_WAIT) == &first && fake_primask_disable_count() == 2);

	/* Emptying and refilling must not leave the tail pointing at a taken item. */
	k_fifo_put(&fifo, &first);
	(void)k_fifo_get(&fifo, K_NO_WAIT);
	k_fifo_put(&fifo, &second);
	CHECK("refilling after the queue drained returns the new item, not the old",
	      k_fifo_get(&fifo, K_NO_WAIT) == &second);

	CHECK("a bit starts clear", !atomic_test_bit(events, 3));
	fake_primask_reset();
	atomic_set_bit(events, 3);
	CHECK("setting a bit masks interrupts, because a callout sets it",
	      atomic_test_bit(events, 3) && fake_primask_disable_count() == 1 &&
		      fake_primask_get() == 0);
	atomic_set_bit(events, 0);
	CHECK("bits are independent",
	      atomic_test_bit(events, 0) && atomic_test_bit(events, 3));
	atomic_clear_bit(events, 3);
	CHECK("clearing one leaves the other alone",
	      !atomic_test_bit(events, 3) && atomic_test_bit(events, 0));

	CHECK("nothing here reached the fatal path", g_fatal_calls == 0);

	printf("RESULT: %s (%u checks)\n", g_failures == 0 ? "PASS" : "FAIL", g_checks);
	return g_failures == 0 ? 0 : 1;
}
