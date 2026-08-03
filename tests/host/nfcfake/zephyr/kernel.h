/* nfcfake: <zephyr/kernel.h> — threads, semaphores, sleeps and the uptime
 * clock, enough for the PN532 transport's polling thread.
 *
 * NOT logfake's kernel.h: this one carries the thread/semaphore surface that
 * one does not, and the two are never in the same binary.
 *
 * Every wait (k_msleep, k_sem_take) spends one tick of the budget
 * nfcfake_run_thread() set. Spending the last one longjmps out of the thread
 * loop, which is the only way to leave a `for (;;)`. */
#ifndef NFCFAKE_ZEPHYR_KERNEL_H
#define NFCFAKE_ZEPHYR_KERNEL_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif
#ifndef printk
#define printk printf
#endif
#ifndef snprintk
#define snprintk snprintf
#endif

typedef int64_t k_timeout_t; /* milliseconds in this fake */
#define K_MSEC(ms)    ((k_timeout_t)(ms))
#define K_SECONDS(s)  ((k_timeout_t)(s) * 1000)
#define K_NO_WAIT     ((k_timeout_t)0)
#define K_FOREVER     ((k_timeout_t)-1)

/* C linkage throughout: pn532_bus_spi.c is C, transport_pn532.cpp is C++, and
 * the fake that implements all of it is C++. */
#ifdef __cplusplus
extern "C" {
#endif

int64_t k_uptime_get(void);
void k_msleep(int ms);

/* ---- semaphores ----------------------------------------------------------- */

struct k_sem {
	unsigned count;
	unsigned limit;
};

#define K_SEM_DEFINE(name, initial, maximum) struct k_sem name = {(initial), (maximum)}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);
void k_sem_reset(struct k_sem *sem);
void k_sem_init(struct k_sem *sem, unsigned initial, unsigned maximum);

/* ---- threads -------------------------------------------------------------- */

struct k_thread {
	int unused;
};
typedef struct k_thread *k_tid_t;
typedef void (*k_thread_entry_t)(void *, void *, void *);

#define K_THREAD_STACK_DEFINE(name, size) char name[size]
#define K_THREAD_STACK_SIZEOF(stack)      sizeof(stack)
#define K_PRIO_PREEMPT(x)                 (x)

k_tid_t k_thread_create(struct k_thread *thread, void *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay);
void k_thread_name_set(k_tid_t thread, const char *name);

#ifdef __cplusplus
}
#endif

/* ---- work items ----------------------------------------------------------- */

struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);
struct k_work {
	k_work_handler_t handler;
};

#define K_WORK_DEFINE(name, fn) struct k_work name = {(fn)}

/* ---- atomics --------------------------------------------------------------
 * Single-threaded by construction here, so plain loads and stores are exactly
 * as correct as the real atomics and nothing is being papered over. */
typedef long atomic_t;

static inline long atomic_get(const atomic_t *target)
{
	return *target;
}
static inline long atomic_set(atomic_t *target, long value)
{
	long old = *target;

	*target = value;
	return old;
}
static inline long atomic_clear(atomic_t *target)
{
	return atomic_set(target, 0);
}

#endif /* NFCFAKE_ZEPHYR_KERNEL_H */
