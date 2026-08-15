/* SPDX-License-Identifier: ISC */

/*
 * osal_esp.c - the ESP-IDF backend of ultrawidelock_osal.h: one dispatch task draining
 * a FreeRTOS queue of ultrawidelock_work pointers, with delayable work held in a
 * deadline-ordered list the task sleeps against. Self-contained FreeRTOS --
 * no esp_timer -- so the whole backend is this file. Everything here is
 * task-context only; the DW3000 port's IRQ already defers to a task before
 * any of this can be reached.
 */
#if defined(ESP_PLATFORM)

#include <string.h>

#include "ultrawidelock_osal.h"
#include "ultrawidelock_port.h"

#define OSAL_QUEUE_DEPTH 16
#define OSAL_TASK_STACK  4096

static StaticQueue_t g_queue_buf;
static uint8_t g_queue_storage[OSAL_QUEUE_DEPTH * sizeof(struct ultrawidelock_work *)];
static QueueHandle_t g_queue;

static StaticTask_t g_task_tcb;
static StackType_t g_task_stack[OSAL_TASK_STACK / sizeof(StackType_t)];

static ultrawidelock_mutex_t g_dwork_lock;
static struct ultrawidelock_dwork *g_dwork_head; /* deadline-ordered, FIFO among equals */

/* ---- the dispatch task ------------------------------------------------------ */

static void dwork_remove_locked(struct ultrawidelock_dwork *dwork)
{
	struct ultrawidelock_dwork **at = &g_dwork_head;

	while (*at) {
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
		struct ultrawidelock_work *w = NULL;
		TickType_t wait = portMAX_DELAY;

		/* Sleep until the next deadline, or forever if none is armed. */
		ultrawidelock_mutex_lock(&g_dwork_lock);
		if (g_dwork_head) {
			int64_t now = ultrawidelock_uptime_us();
			int64_t due = g_dwork_head->deadline_us;

			wait = due <= now ? 0
					  : pdMS_TO_TICKS((uint32_t)((due - now + 999) / 1000));
		}
		ultrawidelock_mutex_unlock(&g_dwork_lock);

		if (xQueueReceive(g_queue, &w, wait) == pdTRUE && w != NULL) {
			w->pending = 0;
			w->fn(w);
		}

		/* Run whatever fell due while we slept or worked. */
		for (;;) {
			struct ultrawidelock_dwork *d = NULL;

			ultrawidelock_mutex_lock(&g_dwork_lock);
			if (g_dwork_head && g_dwork_head->deadline_us <= ultrawidelock_uptime_us()) {
				d = g_dwork_head;
				g_dwork_head = d->next;
				d->next = NULL;
				d->pending = 0; /* cleared first, so it may re-arm */
			}
			ultrawidelock_mutex_unlock(&g_dwork_lock);
			if (d == NULL) {
				break;
			}
			d->fn(d);
		}
	}
}

static void osal_start_once(void)
{
	if (g_queue != NULL) {
		return;
	}
	ultrawidelock_mutex_init(&g_dwork_lock);
	g_queue = xQueueCreateStatic(OSAL_QUEUE_DEPTH, sizeof(struct ultrawidelock_work *),
				     g_queue_storage, &g_queue_buf);
	xTaskCreateStatic(dispatch_task, "ultrawidelock_osal", sizeof(g_task_stack) / sizeof(StackType_t),
			  NULL, tskIDLE_PRIORITY + 2, g_task_stack, &g_task_tcb);
}

/* ---- init hooks ------------------------------------------------------------- */

#define OSAL_MAX_INIT 16
static int (*g_init_fns[OSAL_MAX_INIT])(void);
static unsigned g_init_count;
static int g_init_ran;

void ultrawidelock_osal_init_register(int (*fn)(void))
{
	if (g_init_count < OSAL_MAX_INIT) {
		g_init_fns[g_init_count++] = fn;
	}
}

int ultrawidelock_osal_init_all(void)
{
	int rc = 0;

	if (g_init_ran) {
		return 0;
	}
	g_init_ran = 1;
	osal_start_once();
	for (unsigned i = 0; i < g_init_count; i++) {
		int r = g_init_fns[i]();

		if (r != 0 && rc == 0) {
			rc = r;
		}
	}
	return rc;
}

/* ---- immediate work --------------------------------------------------------- */

void ultrawidelock_work_init(struct ultrawidelock_work *work, ultrawidelock_work_fn fn)
{
	work->fn = fn;
	work->next = NULL;
	work->pending = 0;
}

int ultrawidelock_work_submit(struct ultrawidelock_work *work)
{
	osal_start_once();
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

/* ---- delayable work --------------------------------------------------------- */

void ultrawidelock_dwork_init(struct ultrawidelock_dwork *dwork, ultrawidelock_dwork_fn fn)
{
	dwork->fn = fn;
	dwork->next = NULL;
	dwork->deadline_us = 0;
	dwork->pending = 0;
}

/* A dummy queue item wakes the task so it re-reads the nearest deadline. */
static void poke_dispatch(void)
{
	struct ultrawidelock_work *none = NULL;

	(void)xQueueSend(g_queue, &none, 0);
}

static int dwork_arm(struct ultrawidelock_dwork *dwork, int32_t delay_ms, int restart)
{
	osal_start_once();
	ultrawidelock_mutex_lock(&g_dwork_lock);
	if (dwork->pending && !restart) {
		ultrawidelock_mutex_unlock(&g_dwork_lock);
		return 0; /* the earlier deadline stands */
	}
	if (dwork->pending) {
		dwork_remove_locked(dwork);
	}
	dwork->pending = 1;
	dwork->deadline_us = ultrawidelock_uptime_us() + (int64_t)delay_ms * 1000;
	struct ultrawidelock_dwork **at = &g_dwork_head;

	while (*at && (*at)->deadline_us <= dwork->deadline_us) {
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
	ultrawidelock_mutex_lock(&g_dwork_lock);
	if (dwork->pending) {
		dwork_remove_locked(dwork);
		dwork->pending = 0;
	}
	ultrawidelock_mutex_unlock(&g_dwork_lock);
	return 0;
}

/* ---- semaphore -------------------------------------------------------------- */

void ultrawidelock_sem_init(ultrawidelock_sem_t *sem, unsigned initial, unsigned limit)
{
	sem->h = xSemaphoreCreateCountingStatic(limit, initial, &sem->buf);
}

void ultrawidelock_sem_give(ultrawidelock_sem_t *sem)
{
	xSemaphoreGive(sem->h); /* fails silently at the limit: saturation */
}

int ultrawidelock_sem_take(ultrawidelock_sem_t *sem, int32_t timeout_ms)
{
	TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS((uint32_t)timeout_ms);

	return xSemaphoreTake(sem->h, t) == pdTRUE ? 0 : -1;
}

void ultrawidelock_sem_reset(ultrawidelock_sem_t *sem)
{
	while (xSemaphoreTake(sem->h, 0) == pdTRUE) {
	}
}

/* ---- thread ----------------------------------------------------------------- */

int ultrawidelock_thread_create(ultrawidelock_thread_t *thread, ultrawidelock_thread_stack_t *stack,
				size_t stack_size, void (*entry)(void *arg), void *arg,
				enum ultrawidelock_thread_prio prio, const char *name)
{
	static const UBaseType_t prios[] = {
		[ULTRAWIDELOCK_THREAD_PRIO_LOW] = tskIDLE_PRIORITY + 1,
		[ULTRAWIDELOCK_THREAD_PRIO_NORM] = tskIDLE_PRIORITY + 2,
		[ULTRAWIDELOCK_THREAD_PRIO_HIGH] = tskIDLE_PRIORITY + 3,
	};

	thread->handle = xTaskCreateStatic(entry, name ? name : "ultrawidelock", stack_size / sizeof(StackType_t),
					   arg, prios[prio], stack, &thread->tcb);
	return thread->handle != NULL ? 0 : -1;
}

#endif /* ESP_PLATFORM */
