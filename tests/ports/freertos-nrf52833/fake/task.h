#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H

#include "FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t stack_depth,
			       void *arg, UBaseType_t priority, StackType_t *stack,
			       StaticTask_t *task_storage);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher_priority_task_woken);
uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks);

/*
 * The tick count, which the board's uptime hook extends past its 32-bit wrap.
 * The model lets a test set it to any value, including one just short of the
 * wrap, which is the only way to reach that extension in a bounded run.
 */
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void fake_task_set_tick_count(TickType_t ticks);
void fake_task_advance_tick_count(TickType_t ticks);

/*
 * Scheduler state and critical sections, for code that must decide whether
 * locking is possible at all. The flash and key-value layers both take a mutex
 * only once the scheduler is running -- before that there is nothing to contend
 * with, and taking one would be a call into a kernel that has not started.
 *
 * The model defaults to RUNNING so the locked path is what tests exercise
 * unless they say otherwise; fake_task_set_scheduler_state() reaches the other
 * branch, which is the one that runs during bring-up.
 */
#define taskSCHEDULER_NOT_STARTED 0
#define taskSCHEDULER_SUSPENDED	  1
#define taskSCHEDULER_RUNNING	  2

BaseType_t xTaskGetSchedulerState(void);
void fake_task_set_scheduler_state(BaseType_t state);

/* Counted rather than merely flagged, so a test can assert they nest and
 * balance instead of only that they were reached. */
void vTaskEnterCritical(void);
void vTaskExitCritical(void);
unsigned fake_task_critical_depth(void);
unsigned fake_task_critical_entries(void);

#define taskENTER_CRITICAL() vTaskEnterCritical()
#define taskEXIT_CRITICAL()  vTaskExitCritical()

#endif /* TEST_FREERTOS_TASK_H */
