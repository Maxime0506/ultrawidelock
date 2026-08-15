/* SPDX-License-Identifier: ISC */

/*
 * osal_zephyr.c - the Zephyr backend of ultrawidelock_osal.h: 1:1 onto k_work,
 * k_work_delayable, k_sem and k_thread on the system workqueue, plus the
 * container-of trampolines the k_work handler signature needs. Semantics are
 * Zephyr's own, so converted modules behave exactly as they did before the
 * contract existed.
 */
#if defined(__ZEPHYR__)

#include "ultrawidelock_osal.h"

static void work_tramp(struct k_work *kw)
{
	struct ultrawidelock_work *w = CONTAINER_OF(kw, struct ultrawidelock_work, kw);

	w->fn(w);
}

static void dwork_tramp(struct k_work *kw)
{
	struct k_work_delayable *dk = k_work_delayable_from_work(kw);
	struct ultrawidelock_dwork *w = CONTAINER_OF(dk, struct ultrawidelock_dwork, kw);

	w->fn(w);
}

void ultrawidelock_work_init(struct ultrawidelock_work *work, ultrawidelock_work_fn fn)
{
	work->fn = fn;
	k_work_init(&work->kw, work_tramp);
}

int ultrawidelock_work_submit(struct ultrawidelock_work *work)
{
	int rc = k_work_submit(&work->kw);

	return rc >= 0 ? 0 : rc;
}

void ultrawidelock_dwork_init(struct ultrawidelock_dwork *dwork, ultrawidelock_dwork_fn fn)
{
	dwork->fn = fn;
	k_work_init_delayable(&dwork->kw, dwork_tramp);
}

int ultrawidelock_dwork_schedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	int rc = k_work_schedule(&dwork->kw, K_MSEC(delay_ms));

	return rc >= 0 ? 0 : rc;
}

int ultrawidelock_dwork_reschedule(struct ultrawidelock_dwork *dwork, int32_t delay_ms)
{
	int rc = k_work_reschedule(&dwork->kw, K_MSEC(delay_ms));

	return rc >= 0 ? 0 : rc;
}

int ultrawidelock_dwork_cancel(struct ultrawidelock_dwork *dwork)
{
	k_work_cancel_delayable(&dwork->kw);
	return 0;
}

void ultrawidelock_sem_init(ultrawidelock_sem_t *sem, unsigned initial, unsigned limit)
{
	k_sem_init(sem, initial, limit);
}

void ultrawidelock_sem_give(ultrawidelock_sem_t *sem)
{
	k_sem_give(sem);
}

int ultrawidelock_sem_take(ultrawidelock_sem_t *sem, int32_t timeout_ms)
{
	k_timeout_t t = timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms);

	return k_sem_take(sem, t) == 0 ? 0 : -1;
}

void ultrawidelock_sem_reset(ultrawidelock_sem_t *sem)
{
	k_sem_reset(sem);
}

static void thread_tramp(void *entry, void *arg, void *unused)
{
	ARG_UNUSED(unused);
	((void (*)(void *))entry)(arg);
}

int ultrawidelock_thread_create(ultrawidelock_thread_t *thread, ultrawidelock_thread_stack_t *stack,
				size_t stack_size, void (*entry)(void *arg), void *arg,
				enum ultrawidelock_thread_prio prio, const char *name)
{
	/* NORM is K_PRIO_PREEMPT(7): the NFC transport thread's historical
	 * level, kept exact so the conversion changes no scheduling. */
	static const int prios[] = {
		[ULTRAWIDELOCK_THREAD_PRIO_LOW] = K_PRIO_PREEMPT(12),
		[ULTRAWIDELOCK_THREAD_PRIO_NORM] = K_PRIO_PREEMPT(7),
		[ULTRAWIDELOCK_THREAD_PRIO_HIGH] = K_PRIO_PREEMPT(2),
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
