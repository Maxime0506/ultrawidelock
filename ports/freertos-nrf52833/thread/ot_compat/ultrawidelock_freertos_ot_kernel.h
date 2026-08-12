/*
 * The Zephyr kernel objects the pinned OpenThread radio platform uses, on
 * FreeRTOS.
 *
 * radio_nrf5.c needs exactly two: one semaphore, which the RSSI read waits on
 * and a driver callout signals, and one intrusive FIFO holding the receive
 * buffer pool. Both are declared inside the platform's own static state, so
 * they are objects rather than handles and neither can be allocated.
 *
 * This header lives outside ot_compat/zephyr/ so that no port source line has
 * to name a Zephyr path; the shim header includes it.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_OT_KERNEL_H
#define ULTRAWIDELOCK_FREERTOS_OT_KERNEL_H

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <ultrawidelock_freertos_vendor_macros.h>

/*
 * Zephyr's timeout type. Only the two extremes are used, so the value is the
 * FreeRTOS tick count to block for and needs no conversion.
 */
typedef struct {
	uint32_t ticks;
} k_timeout_t;

#define K_NO_WAIT ((k_timeout_t){0})
#define K_FOREVER ((k_timeout_t){portMAX_DELAY})

/*
 * Priorities. Nothing in the platform schedules with these; they reach one
 * assertion about the context a frame is handed over in.
 */
#define K_PRIO_COOP(x) (-(configMAX_PRIORITIES) + (x))
#define K_PRIO_PREEMPT(x) (x)

struct k_sem {
	StaticSemaphore_t storage;
	SemaphoreHandle_t handle;
};

void k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit);
/* Safe from an interrupt, as Zephyr's is: the driver callouts signal from one. */
void k_sem_give(struct k_sem *sem);
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);

/*
 * The intrusive FIFO. Zephyr reserves the first word of every queued item for
 * the link, and the platform's receive frame does the same, so items are
 * pushed by address with no allocation anywhere.
 */
struct k_fifo {
	void *head;
	void *tail;
};

void k_fifo_init(struct k_fifo *fifo);
void k_fifo_put(struct k_fifo *fifo, void *item);
/* Only K_NO_WAIT is used, and a blocking get is not provided. */
void *k_fifo_get(struct k_fifo *fifo, k_timeout_t timeout);

/* Zephyr's unrecoverable-error path. This must not return. */
_Noreturn void k_oops(void);

/*
 * Zephyr's atomic bit array. The radio platform records pending work in one of
 * these from driver callouts and reads it from the OpenThread task, so the
 * operations have to exclude an interrupt rather than merely a task switch.
 */
typedef unsigned long atomic_t;

#define ULTRAWIDELOCK_OT_ATOMIC_BITS (sizeof(atomic_t) * 8u)
#define ATOMIC_DEFINE(name, nbits)                                                                 \
	atomic_t name[((nbits) + ULTRAWIDELOCK_OT_ATOMIC_BITS - 1u) / ULTRAWIDELOCK_OT_ATOMIC_BITS]

bool atomic_test_bit(const atomic_t *target, int bit);
void atomic_set_bit(atomic_t *target, int bit);
void atomic_clear_bit(atomic_t *target, int bit);

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define ARG_UNUSED(x) ((void)(x))

/* Zephyr's time unit constants, used to state delays in nanoseconds. */
#define NSEC_PER_USEC 1000U
#define USEC_PER_MSEC 1000U
#define NSEC_PER_MSEC (NSEC_PER_USEC * USEC_PER_MSEC)

#endif /* ULTRAWIDELOCK_FREERTOS_OT_KERNEL_H */
