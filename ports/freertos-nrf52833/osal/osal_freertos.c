/* SPDX-License-Identifier: ISC */

/*
 * Standalone FreeRTOS backend for ultrawidelock_osal.h.
 *
 * All kernel objects and task stacks are static. Callers use this API from
 * task context. A board ISR must wake its dedicated worker task before
 * calling shared UWB code.
 */
#if defined(ULTRAWIDELOCK_PORT_FREERTOS)

#include <string.h>

/*
 * FreeRTOS.h first, and not only by convention: the kernel's own queue.h,
 * semphr.h and task.h open with an #error if it has not been seen, because they
 * are written against types and configuration it defines. Every other file in
 * this port already had the order right; this one did not, and the host doubles
 * were permissive enough not to say so.
 */
#include "FreeRTOS.h"

#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "ultrawidelock_osal.h"
#include "ultrawidelock_port.h"

#ifndef ULTRAWIDELOCK_FREERTOS_OSAL_QUEUE_DEPTH
#define ULTRAWIDELOCK_FREERTOS_OSAL_QUEUE_DEPTH 16
#endif

/*
 * Sized from paint, not guessed: 1,224 B peak read off the hardware on
 * 2026-08-14 after commissioning-restore plus a full UWB walk-up unlock,
 * the heaviest workload this dispatcher has run. 2,560 keeps 2x the peak;
 * configCHECK_FOR_STACK_OVERFLOW=2 names any future violation.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_OSAL_STACK_BYTES
#define ULTRAWIDELOCK_FREERTOS_OSAL_STACK_BYTES 2560
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_OSAL_TASK_PRIORITY
#define ULTRAWIDELOCK_FREERTOS_OSAL_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
#endif

static StaticQueue_t g_queue_buf;
static uint8_t g_queue_storage[ULTRAWIDELOCK_FREERTOS_OSAL_QUEUE_DEPTH *
			       sizeof(struct ultrawidelock_work *)];
static QueueHandle_t g_queue;

static StaticTask_t g_task_tcb;
static StackType_t
	g_task_stack[(ULTRAWIDELOCK_FREERTOS_OSAL_STACK_BYTES + sizeof(StackType_t) - 1) /
		     sizeof(StackType_t)];
static TaskHandle_t g_task;

static ultrawidelock_mutex_t g_dwork_lock;
static struct ultrawidelock_dwork *g_dwork_head;

static void dwork_remove_locked(struct ultrawidelock_dwork *dwork)
{
	struct ultrawidelock_dwork **at = &g_dwork_head;

	while (*at != NULL) {
		if (*at == dwork) {
			*at = dwork->next;
			dwork->next = NULL;
			return;
		}
		at = &(*at)->next;
	}
}

static void dispatch_task(void *arg)
{
	(void)arg;
	for (;;) {
		struct ultrawidelock_work *work = NULL;
		TickType_t wait = portMAX_DELAY;

		ultrawidelock_mutex_lock(&g_dwork_lock);
		if (g_dwork_head != NULL) {
			int64_t now = ultrawidelock_uptime_us();
			int64_t due = g_dwork_head->deadline_us;

			wait = due <= now ? 0
					  : pdMS_TO_TICKS((uint32_t)((due - now + 999) / 1000));
			if (due > now && wait == 0) {
				wait = 1;
			}
		}
		ultrawidelock_mutex_unlock(&g_dwork_lock);

		if (xQueueReceive(g_queue, &work, wait) == pdTRUE && work != NULL) {
			work->pending = 0;
			work->fn(work);
		}

		for (;;) {
			struct ultrawidelock_dwork *dwork = NULL;

			ultrawidelock_mutex_lock(&g_dwork_lock);
			if (g_dwork_head != NULL &&
			    g_dwork_head->deadline_us <= ultrawidelock_uptime_us()) {
				dwork = g_dwork_head;
				g_dwork_head = dwork->next;
				dwork->next = NULL;
				dwork->pending = 0;
			}
			ultrawidelock_mutex_unlock(&g_dwork_lock);
			if (dwork == NULL) {
				break;
			}
			dwork->fn(dwork);
		}
	}
}

static int osal_start_once(void)
{
	if (g_task != NULL) {
		return 0;
	}

	ultrawidelock_mutex_init(&g_dwork_lock);
	g_queue = xQueueCreateStatic(ULTRAWIDELOCK_FREERTOS_OSAL_QUEUE_DEPTH,
				     sizeof(struct ultrawidelock_work *), g_queue_storage, &g_queue_buf);
	if (g_queue == NULL) {
		return -1;
	}

	g_task = xTaskCreateStatic(dispatch_task, "ultrawidelock_osal",
				   sizeof(g_task_stack) / sizeof(g_task_stack[0]), NULL,
				   ULTRAWIDELOCK_FREERTOS_OSAL_TASK_PRIORITY, g_task_stack, &g_task_tcb);
	if (g_task == NULL) {
		g_queue = NULL;
		return -1;
	}
	return 0;
}

#define OSAL_MAX_INIT 16
static int (*g_init_fns[OSAL_MAX_INIT])(void);
static unsigned g_init_count;
static int g_init_ran;

void ultrawidelock_osal_init_register(int (*fn)(void))
{
	if (fn != NULL && g_init_count < OSAL_MAX_INIT) {
		g_init_fns[g_init_count++] = fn;
	}
}

int ultrawidelock_osal_init_all(void)
{
	int rc = 0;

	if (g_init_ran) {
		return 0;
	}
	if (osal_start_once() != 0) {
		return -1;
	}
	g_init_ran = 1;
	for (unsigned i = 0; i < g_init_count; i++) {
		int hook_rc = g_init_fns[i]();

		if (hook_rc != 0 && rc == 0) {
			rc = hook_rc;
		}
	}
	return rc;
}

void ultrawidelock_work_init(struct ultrawidelock_work *work, ultrawidelock_work_fn fn)
{
	work->fn = fn;
	work->next = NULL;
	work->pending = 0;
}

int ultrawidelock_work_submit(struct ultrawidelock_work *work)
{
	if (work == NULL || work->fn == NULL || osal_start_once() != 0) {
		return -1;
	}
	if (work->pending) {
		return 0;
	}
	work->pending = 1;
	if (xQueueSend(g_queue, &work, 0) != pdTRUE) {
		work->pending = 0;
		return -1;
	}
	return 0;
}

void ultrawidelock_dwork_init(struct ultrawidelock_dwork *dwork, ultrawidelock_dwork_fn fn)
{
	dwork->fn = fn;
	dwork->next = NULL;
	dwork->deadline_us = 0;
	dwork->pending = 0;
}

static void poke_dispatch(void)
{
	struct ultrawidelock_work *none = NULL;

	(void)xQueueSend(g_queue, &none, 0);
}

static int dwork_arm(struct ultrawidelock_dwork *dwork, int32_t delay_ms, int restart)
{
	struct ultrawidelock_dwork **at;

	if (dwork == NULL || dwork->fn == NULL || delay_ms < 0 || osal_start_once() != 0) {
		return -1;
	}
	ultrawidelock_mutex_lock(&g_dwork_lock);
	if (dwork->pending && !restart) {
		ultrawidelock_mutex_unlock(&g_dwork_lock);
		return 0;
	}
	if (dwork->pending) {
		dwork_remove_locked(dwork);
	}
	dwork->pending = 1;
	dwork->deadline_us = ultrawidelock_uptime_us() + (int64_t)delay_ms * 1000;
	at = &g_dwork_head;
	while (*at != NULL && (*at)->deadline_us <= dwork->deadline_us) {
		at = &(*at)->next;
	}
	dwork->next = *at;
	*at = dwork;
	ultrawidelock_mutex_unlock(&g_dwork_lock);
	poke_dispatch();
	return 0;
}

int ultrawidelock_dwork_schedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	return dwork_arm(dwork, delay_ms, 0);
}

int ultrawidelock_dwork_reschedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	return dwork_arm(dwork, delay_ms, 1);
}

int ultrawidelock_dwork_cancel(struct ultrawidelock_dwork *dwork)
{
	if (dwork == NULL) {
		return -1;
	}
	if (!dwork->pending) {
		return 0;
	}
	ultrawidelock_mutex_lock(&g_dwork_lock);
	dwork_remove_locked(dwork);
	dwork->pending = 0;
	ultrawidelock_mutex_unlock(&g_dwork_lock);
	return 0;
}

void ultrawidelock_sem_init(ultrawidelock_sem_t *sem, unsigned initial, unsigned limit)
{
	sem->h = xSemaphoreCreateCountingStatic(limit, initial, &sem->buf);
}

void ultrawidelock_sem_give(ultrawidelock_sem_t *sem)
{
	(void)xSemaphoreGive(sem->h);
}

int ultrawidelock_sem_take(ultrawidelock_sem_t *sem, int32_t timeout_ms)
{
	TickType_t ticks =
		timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS((uint32_t)timeout_ms);
	if (timeout_ms > 0 && ticks == 0) {
		ticks = 1;
	}

	return xSemaphoreTake(sem->h, ticks) == pdTRUE ? 0 : -1;
}

void ultrawidelock_sem_reset(ultrawidelock_sem_t *sem)
{
	while (xSemaphoreTake(sem->h, 0) == pdTRUE) {
	}
}

int ultrawidelock_thread_create(ultrawidelock_thread_t *thread, ultrawidelock_thread_stack_t *stack,
				size_t stack_size, void (*entry)(void *arg), void *arg,
				enum ultrawidelock_thread_prio prio, const char *name)
{
	static const UBaseType_t priorities[] = {
		[ULTRAWIDELOCK_THREAD_PRIO_LOW] = tskIDLE_PRIORITY + 1,
		[ULTRAWIDELOCK_THREAD_PRIO_NORM] = tskIDLE_PRIORITY + 2,
		[ULTRAWIDELOCK_THREAD_PRIO_HIGH] = tskIDLE_PRIORITY + 3,
	};

	/*
	 * Only the upper bound is tested. ULTRAWIDELOCK_THREAD_PRIO_LOW is the first
	 * enumerator, so the compiler picks an unsigned type for the enum and a
	 * lower-bound test can never fail; writing one is a target-compile
	 * warning rather than defence. The assertion keeps that reasoning tied
	 * to the enum instead of to this comment.
	 */
	_Static_assert(ULTRAWIDELOCK_THREAD_PRIO_LOW == 0,
		       "a non-zero lowest priority needs a lower-bound check here");
	if (thread == NULL || stack == NULL || entry == NULL || prio > ULTRAWIDELOCK_THREAD_PRIO_HIGH ||
	    stack_size < sizeof(StackType_t)) {
		return -1;
	}
	thread->handle = xTaskCreateStatic(entry, name != NULL ? name : "ultrawidelock",
					   stack_size / sizeof(StackType_t), arg,
					   priorities[prio], stack, &thread->tcb);
	return thread->handle != NULL ? 0 : -1;
}

#endif /* ULTRAWIDELOCK_PORT_FREERTOS */
