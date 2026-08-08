/*
 * osal_esp.c - the ESP-IDF backend of woz_osal.h: one dispatch task draining
 * a FreeRTOS queue of woz_work pointers, with delayable work held in a
 * deadline-ordered list the task sleeps against. Self-contained FreeRTOS --
 * no esp_timer -- so the whole backend is this file. Everything here is
 * task-context only; the DW3000 port's IRQ already defers to a task before
 * any of this can be reached.
 */
#if defined(ESP_PLATFORM)

#include <string.h>

#include "woz_osal.h"
#include "woz_port.h"

#define OSAL_QUEUE_DEPTH 16
#define OSAL_TASK_STACK  4096

static StaticQueue_t g_queue_buf;
static uint8_t g_queue_storage[OSAL_QUEUE_DEPTH * sizeof(struct woz_work *)];
static QueueHandle_t g_queue;

static StaticTask_t g_task_tcb;
static StackType_t g_task_stack[OSAL_TASK_STACK / sizeof(StackType_t)];

static woz_mutex_t g_dwork_lock;
static struct woz_dwork *g_dwork_head; /* deadline-ordered, FIFO among equals */

/* ---- the dispatch task ------------------------------------------------------ */

static void dwork_remove_locked(struct woz_dwork *dwork)
{
	struct woz_dwork **at = &g_dwork_head;

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
		struct woz_work *w = NULL;
		TickType_t wait = portMAX_DELAY;

		/* Sleep until the next deadline, or forever if none is armed. */
		woz_mutex_lock(&g_dwork_lock);
		if (g_dwork_head) {
			int64_t now = woz_uptime_us();
			int64_t due = g_dwork_head->deadline_us;

			wait = due <= now ? 0
					  : pdMS_TO_TICKS((uint32_t)((due - now + 999) / 1000));
		}
		woz_mutex_unlock(&g_dwork_lock);

		if (xQueueReceive(g_queue, &w, wait) == pdTRUE && w != NULL) {
			w->pending = 0;
			w->fn(w);
		}

		/* Run whatever fell due while we slept or worked. */
		for (;;) {
			struct woz_dwork *d = NULL;

			woz_mutex_lock(&g_dwork_lock);
			if (g_dwork_head && g_dwork_head->deadline_us <= woz_uptime_us()) {
				d = g_dwork_head;
				g_dwork_head = d->next;
				d->next = NULL;
				d->pending = 0; /* cleared first, so it may re-arm */
			}
			woz_mutex_unlock(&g_dwork_lock);
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
	woz_mutex_init(&g_dwork_lock);
	g_queue = xQueueCreateStatic(OSAL_QUEUE_DEPTH, sizeof(struct woz_work *),
				     g_queue_storage, &g_queue_buf);
	xTaskCreateStatic(dispatch_task, "woz_osal", sizeof(g_task_stack) / sizeof(StackType_t),
			  NULL, tskIDLE_PRIORITY + 2, g_task_stack, &g_task_tcb);
}

/* ---- init hooks ------------------------------------------------------------- */

#define OSAL_MAX_INIT 16
static int (*g_init_fns[OSAL_MAX_INIT])(void);
static unsigned g_init_count;
static int g_init_ran;

void woz_osal_init_register(int (*fn)(void))
{
	if (g_init_count < OSAL_MAX_INIT) {
		g_init_fns[g_init_count++] = fn;
	}
}

int woz_osal_init_all(void)
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

void woz_work_init(struct woz_work *work, woz_work_fn fn)
{
	work->fn = fn;
	work->next = NULL;
	work->pending = 0;
}

int woz_work_submit(struct woz_work *work)
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

void woz_dwork_init(struct woz_dwork *dwork, woz_dwork_fn fn)
{
	dwork->fn = fn;
	dwork->next = NULL;
	dwork->deadline_us = 0;
	dwork->pending = 0;
}

/* A dummy queue item wakes the task so it re-reads the nearest deadline. */
static void poke_dispatch(void)
{
	struct woz_work *none = NULL;

	(void)xQueueSend(g_queue, &none, 0);
}

static int dwork_arm(struct woz_dwork *dwork, int32_t delay_ms, int restart)
{
	osal_start_once();
	woz_mutex_lock(&g_dwork_lock);
	if (dwork->pending && !restart) {
		woz_mutex_unlock(&g_dwork_lock);
		return 0; /* the earlier deadline stands */
	}
	if (dwork->pending) {
		dwork_remove_locked(dwork);
	}
	dwork->pending = 1;
	dwork->deadline_us = woz_uptime_us() + (int64_t)delay_ms * 1000;
	struct woz_dwork **at = &g_dwork_head;

	while (*at && (*at)->deadline_us <= dwork->deadline_us) {
		at = &(*at)->next;
	}
	dwork->next = *at;
	*at = dwork;
	woz_mutex_unlock(&g_dwork_lock);
	poke_dispatch();
	return 0;
}

int woz_dwork_schedule(struct woz_dwork *dwork, int32_t delay_ms)
{
	return dwork_arm(dwork, delay_ms, 0);
}

int woz_dwork_reschedule(struct woz_dwork *dwork, int32_t delay_ms)
{
	return dwork_arm(dwork, delay_ms, 1);
}

int woz_dwork_cancel(struct woz_dwork *dwork)
{
	woz_mutex_lock(&g_dwork_lock);
	if (dwork->pending) {
		dwork_remove_locked(dwork);
		dwork->pending = 0;
	}
	woz_mutex_unlock(&g_dwork_lock);
	return 0;
}

/* ---- semaphore -------------------------------------------------------------- */

void woz_sem_init(woz_sem_t *sem, unsigned initial, unsigned limit)
{
	sem->h = xSemaphoreCreateCountingStatic(limit, initial, &sem->buf);
}

void woz_sem_give(woz_sem_t *sem)
{
	xSemaphoreGive(sem->h); /* fails silently at the limit: saturation */
}

int woz_sem_take(woz_sem_t *sem, int32_t timeout_ms)
{
	TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS((uint32_t)timeout_ms);

	return xSemaphoreTake(sem->h, t) == pdTRUE ? 0 : -1;
}

void woz_sem_reset(woz_sem_t *sem)
{
	while (xSemaphoreTake(sem->h, 0) == pdTRUE) {
	}
}

/* ---- thread ----------------------------------------------------------------- */

int woz_thread_create(woz_thread_t *thread, woz_thread_stack_t *stack, size_t stack_size,
		      void (*entry)(void *arg), void *arg, enum woz_thread_prio prio,
		      const char *name)
{
	static const UBaseType_t prios[] = {
		[WOZ_THREAD_PRIO_LOW] = tskIDLE_PRIORITY + 1,
		[WOZ_THREAD_PRIO_NORM] = tskIDLE_PRIORITY + 2,
		[WOZ_THREAD_PRIO_HIGH] = tskIDLE_PRIORITY + 3,
	};

	thread->handle = xTaskCreateStatic(entry, name ? name : "woz", stack_size / sizeof(StackType_t),
					   arg, prios[prio], stack, &thread->tcb);
	return thread->handle != NULL ? 0 : -1;
}

#endif /* ESP_PLATFORM */
