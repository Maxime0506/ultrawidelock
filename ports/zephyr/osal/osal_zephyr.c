/*
 * osal_zephyr.c - the Zephyr backend of woz_osal.h: 1:1 onto k_work,
 * k_work_delayable, k_sem and k_thread on the system workqueue, plus the
 * container-of trampolines the k_work handler signature needs. Semantics are
 * Zephyr's own, so converted modules behave exactly as they did before the
 * contract existed.
 */
#if defined(__ZEPHYR__)

#include "woz_osal.h"

static void work_tramp(struct k_work *kw)
{
	struct woz_work *w = CONTAINER_OF(kw, struct woz_work, kw);

	w->fn(w);
}

static void dwork_tramp(struct k_work *kw)
{
	struct k_work_delayable *dk = k_work_delayable_from_work(kw);
	struct woz_dwork *w = CONTAINER_OF(dk, struct woz_dwork, kw);

	w->fn(w);
}

void woz_work_init(struct woz_work *work, woz_work_fn fn)
{
	work->fn = fn;
	k_work_init(&work->kw, work_tramp);
}

int woz_work_submit(struct woz_work *work)
{
	int rc = k_work_submit(&work->kw);

	return rc >= 0 ? 0 : rc;
}

void woz_dwork_init(struct woz_dwork *dwork, woz_dwork_fn fn)
{
	dwork->fn = fn;
	k_work_init_delayable(&dwork->kw, dwork_tramp);
}

int woz_dwork_schedule(struct woz_dwork *dwork, int32_t delay_ms)
{
	int rc = k_work_schedule(&dwork->kw, K_MSEC(delay_ms));

	return rc >= 0 ? 0 : rc;
}

int woz_dwork_reschedule(struct woz_dwork *dwork, int32_t delay_ms)
{
	int rc = k_work_reschedule(&dwork->kw, K_MSEC(delay_ms));

	return rc >= 0 ? 0 : rc;
}

int woz_dwork_cancel(struct woz_dwork *dwork)
{
	k_work_cancel_delayable(&dwork->kw);
	return 0;
}

void woz_sem_init(woz_sem_t *sem, unsigned initial, unsigned limit)
{
	k_sem_init(sem, initial, limit);
}

void woz_sem_give(woz_sem_t *sem)
{
	k_sem_give(sem);
}

int woz_sem_take(woz_sem_t *sem, int32_t timeout_ms)
{
	k_timeout_t t = timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms);

	return k_sem_take(sem, t) == 0 ? 0 : -1;
}

void woz_sem_reset(woz_sem_t *sem)
{
	k_sem_reset(sem);
}

static void thread_tramp(void *entry, void *arg, void *unused)
{
	ARG_UNUSED(unused);
	((void (*)(void *))entry)(arg);
}

int woz_thread_create(woz_thread_t *thread, woz_thread_stack_t *stack, size_t stack_size,
		      void (*entry)(void *arg), void *arg, enum woz_thread_prio prio,
		      const char *name)
{
	/* NORM is K_PRIO_PREEMPT(7): the NFC transport thread's historical
	 * level, kept exact so the conversion changes no scheduling. */
	static const int prios[] = {
		[WOZ_THREAD_PRIO_LOW] = K_PRIO_PREEMPT(12),
		[WOZ_THREAD_PRIO_NORM] = K_PRIO_PREEMPT(7),
		[WOZ_THREAD_PRIO_HIGH] = K_PRIO_PREEMPT(2),
	};
	k_tid_t tid = k_thread_create(&thread->thread, stack, stack_size, thread_tramp,
				      (void *)entry, arg, NULL, prios[prio], 0, K_NO_WAIT);

	if (tid == NULL) {
		return -1;
	}
	if (name != NULL) {
		k_thread_name_set(tid, name);
	}
	return 0;
}

#endif /* __ZEPHYR__ */
