#ifndef TEST_FAKE_FREERTOS_CONTROL_H
#define TEST_FAKE_FREERTOS_CONTROL_H

#include <stdint.h>

#include "FreeRTOS.h"

extern int64_t fake_uptime_us;
extern unsigned fake_malloc_calls;
extern unsigned fake_free_calls;
extern unsigned fake_delay_calls;
extern TickType_t fake_delay_ticks;
extern unsigned fake_task_count;
/* Number of subsequent xTaskCreateStatic calls that must return NULL. */
extern unsigned fake_task_create_failures;
extern void (*fake_task_entry)(void *);
extern void *fake_task_arg;
extern UBaseType_t fake_task_priority;
extern uint32_t fake_task_stack_depth;
extern void (*fake_queue_block_hook)(void);
extern void (*fake_notify_block_hook)(void);
extern unsigned fake_task_notify_calls;
extern unsigned fake_isr_notify_calls;
extern unsigned fake_isr_yield_calls;
extern unsigned fake_recursive_take_calls;
extern unsigned fake_recursive_give_calls;

void fake_freertos_reset(void);

extern unsigned fake_semaphore_isr_gives;

/*
 * Every take and give, across every semaphore. A caller holding a lock is not
 * observable from outside the file that owns it, so the balance is: a path that
 * returns without giving shows up here as a take with no give, and on a lock
 * guarding shared hardware that is a permanent wedge rather than a slow path.
 */
extern unsigned fake_semaphore_takes;
extern unsigned fake_semaphore_gives;

#endif /* TEST_FAKE_FREERTOS_CONTROL_H */
