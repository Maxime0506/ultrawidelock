/*
 * osal_host.c - the host backend of ultrawidelock_osal.h, which IS the test double.
 *
 * A FIFO for immediate work, a deadline-ordered list for delayable work, and
 * a virtual clock the suite steps (ultrawidelock_osal_host_advance_ms / _flush). Runs
 * single-threaded and deterministic: nothing fires until the suite says time
 * passed. ultrawidelock_sem_take with a timeout advances the clock itself, running
 * delayable work that falls due, so a handler's give can satisfy the take
 * without a second thread.
 */
#if defined(ULTRAWIDELOCK_PORT_HOST)

#include "ultrawidelock_osal.h"

/* ---- state ---------------------------------------------------------------- */

static struct ultrawidelock_work *g_work_head, *g_work_tail;
static struct ultrawidelock_dwork *g_dwork_head; /* deadline-ordered, FIFO among equals */
static int64_t g_now_ms;

#define ULTRAWIDELOCK_OSAL_HOST_MAX_INIT 16
static int (*g_init_fns[ULTRAWIDELOCK_OSAL_HOST_MAX_INIT])(void);
static unsigned g_init_count;
static int g_init_ran;

/* ---- init hooks ------------------------------------------------------------ */

void ultrawidelock_osal_init_register(int (*fn)(void))
{
	if (g_init_count < ULTRAWIDELOCK_OSAL_HOST_MAX_INIT) {
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
	for (unsigned i = 0; i < g_init_count; i++) {
		int r = g_init_fns[i]();

		if (r != 0 && rc == 0) {
			rc = r;
		}
	}
	return rc;
}

/* ---- immediate work -------------------------------------------------------- */

void ultrawidelock_work_init(struct ultrawidelock_work *work, ultrawidelock_work_fn fn)
{
	work->fn = fn;
	work->next = NULL;
	work->pending = 0;
}

int ultrawidelock_work_submit(struct ultrawidelock_work *work)
{
	if (work->pending) {
		return 0;
	}
	work->pending = 1;
	work->next = NULL;
	if (g_work_tail) {
		g_work_tail->next = work;
	} else {
		g_work_head = work;
	}
	g_work_tail = work;
	return 0;
}

unsigned ultrawidelock_osal_host_flush(void)
{
	/* Snapshot the queue: what these handlers submit waits for the next
	 * flush, so a self-resubmitting handler is stepped, not spun on. */
	struct ultrawidelock_work *w = g_work_head;
	unsigned ran = 0;

	g_work_head = g_work_tail = NULL;
	while (w) {
		struct ultrawidelock_work *next = w->next;

		w->next = NULL;
		w->pending = 0;
		w->fn(w);
		ran++;
		w = next;
	}
	return ran;
}

/* ---- delayable work -------------------------------------------------------- */

void ultrawidelock_dwork_init(struct ultrawidelock_dwork *dwork, ultrawidelock_dwork_fn fn)
{
	dwork->fn = fn;
	dwork->next = NULL;
	dwork->deadline_ms = 0;
	dwork->pending = 0;
}

static void dwork_insert(struct ultrawidelock_dwork *dwork)
{
	struct ultrawidelock_dwork **at = &g_dwork_head;

	while (*at && (*at)->deadline_ms <= dwork->deadline_ms) {
		at = &(*at)->next;
	}
	dwork->next = *at;
	*at = dwork;
}

static void dwork_remove(struct ultrawidelock_dwork *dwork)
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

int ultrawidelock_dwork_schedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	if (dwork->pending) {
		return 0; /* the earlier deadline stands */
	}
	dwork->pending = 1;
	dwork->deadline_ms = g_now_ms + delay_ms;
	dwork_insert(dwork);
	return 0;
}

int ultrawidelock_dwork_reschedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	if (dwork->pending) {
		dwork_remove(dwork);
	}
	dwork->pending = 1;
	dwork->deadline_ms = g_now_ms + delay_ms;
	dwork_insert(dwork);
	return 0;
}

int ultrawidelock_dwork_cancel(struct ultrawidelock_dwork *dwork)
{
	if (dwork->pending) {
		dwork_remove(dwork);
		dwork->pending = 0;
	}
	return 0;
}

unsigned ultrawidelock_osal_host_advance_ms(int64_t ms)
{
	int64_t target = g_now_ms + ms;
	unsigned ran = 0;

	while (g_dwork_head && g_dwork_head->deadline_ms <= target) {
		struct ultrawidelock_dwork *due = g_dwork_head;

		g_now_ms = due->deadline_ms > g_now_ms ? due->deadline_ms : g_now_ms;
		g_dwork_head = due->next;
		due->next = NULL;
		due->pending = 0; /* cleared first, so the handler may re-arm */
		due->fn(due);
		ran++;
	}
	g_now_ms = target;
	return ran;
}

int64_t ultrawidelock_osal_host_now_ms(void)
{
	return g_now_ms;
}

/* ---- semaphore ------------------------------------------------------------- */

void ultrawidelock_sem_init(ultrawidelock_sem_t *sem, unsigned initial, unsigned limit)
{
	sem->count = initial;
	sem->limit = limit;
}

void ultrawidelock_sem_give(ultrawidelock_sem_t *sem)
{
	if (sem->count < sem->limit) {
		sem->count++;
	}
}

int ultrawidelock_sem_take(ultrawidelock_sem_t *sem, int32_t timeout_ms)
{
	if (sem->count > 0) {
		sem->count--;
		return 0;
	}
	if (timeout_ms == 0) {
		return -1;
	}

	/* Single-threaded: the only sources of a give are queued handlers.
	 * Run what is already queued, then walk delayable work forward inside
	 * the window until a give lands or the window (or the work) runs out. */
	ultrawidelock_osal_host_flush();
	if (timeout_ms < 0) {
		while (sem->count == 0 && g_dwork_head) {
			ultrawidelock_osal_host_advance_ms(g_dwork_head->deadline_ms - g_now_ms);
			ultrawidelock_osal_host_flush();
		}
	} else {
		int64_t target = g_now_ms + timeout_ms;

		while (sem->count == 0 && g_dwork_head &&
		       g_dwork_head->deadline_ms <= target) {
			ultrawidelock_osal_host_advance_ms(g_dwork_head->deadline_ms - g_now_ms);
			ultrawidelock_osal_host_flush();
		}
		if (target > g_now_ms) {
			g_now_ms = target; /* the rest of the wait elapses idle */
		}
	}
	if (sem->count > 0) {
		sem->count--;
		return 0;
	}
	return -1;
}

void ultrawidelock_sem_reset(ultrawidelock_sem_t *sem)
{
	sem->count = 0;
}

/* ---- thread ---------------------------------------------------------------- */

int ultrawidelock_thread_create(ultrawidelock_thread_t *thread, ultrawidelock_thread_stack_t *stack,
				size_t stack_size, void (*entry)(void *arg), void *arg,
				enum ultrawidelock_thread_prio prio, const char *name)
{
	(void)stack;
	(void)stack_size;
	(void)prio;
	thread->entry = entry;
	thread->arg = arg;
	thread->name = name;
	thread->created = 1;
	return 0;
}

/* ---- reset ------------------------------------------------------------------ */

void ultrawidelock_osal_host_reset(void)
{
	while (g_work_head) {
		struct ultrawidelock_work *w = g_work_head;

		g_work_head = w->next;
		w->next = NULL;
		w->pending = 0;
	}
	g_work_tail = NULL;
	while (g_dwork_head) {
		struct ultrawidelock_dwork *d = g_dwork_head;

		g_dwork_head = d->next;
		d->next = NULL;
		d->pending = 0;
	}
	g_now_ms = 0;
}

#endif /* ULTRAWIDELOCK_PORT_HOST */
