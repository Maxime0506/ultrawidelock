#include "ultrawidelock_freertos_mpsl.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#if configSUPPORT_STATIC_ALLOCATION != 1
#error "The MPSL worker requires configSUPPORT_STATIC_ALLOCATION=1"
#endif
#if configUSE_RECURSIVE_MUTEXES != 1
#error "MPSL serialization requires configUSE_RECURSIVE_MUTEXES=1"
#endif
#if configUSE_TASK_NOTIFICATIONS != 1
#error "The MPSL worker requires configUSE_TASK_NOTIFICATIONS=1"
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_MPSL_STACK_BYTES
#define ULTRAWIDELOCK_FREERTOS_MPSL_STACK_BYTES 2048u
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_MPSL_TASK_PRIORITY
#define ULTRAWIDELOCK_FREERTOS_MPSL_TASK_PRIORITY (configMAX_PRIORITIES - 1u)
#endif

#define MPSL_STACK_WORDS                                                                \
	((ULTRAWIDELOCK_FREERTOS_MPSL_STACK_BYTES + sizeof(StackType_t) - 1u) / sizeof(StackType_t))

static void (*s_low_priority_process)(void);
static TaskHandle_t s_task;
static StaticTask_t s_task_storage;
static StackType_t s_stack[MPSL_STACK_WORDS];
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;

bool ultrawidelock_freertos_mpsl_ready(void)
{
	return s_task != NULL && s_lock != NULL;
}

void ultrawidelock_freertos_mpsl_lock(void)
{
	BaseType_t taken;

	configASSERT(s_lock != NULL);
	taken = xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
	configASSERT(taken == pdTRUE);
	(void)taken;
}

void ultrawidelock_freertos_mpsl_unlock(void)
{
	BaseType_t given;

	configASSERT(s_lock != NULL);
	given = xSemaphoreGiveRecursive(s_lock);
	configASSERT(given == pdTRUE);
	(void)given;
}

void ultrawidelock_freertos_mpsl_wake(void)
{
	TaskHandle_t task = s_task;

	if (task != NULL) {
		(void)xTaskNotifyGive(task);
	}
}

void ultrawidelock_freertos_mpsl_wake_from_isr(void)
{
	BaseType_t wake = pdFALSE;
	TaskHandle_t task = s_task;

	if (task != NULL) {
		vTaskNotifyGiveFromISR(task, &wake);
		portYIELD_FROM_ISR(wake);
	}
}

static void mpsl_task(void *arg)
{
	(void)arg;
	s_task = xTaskGetCurrentTaskHandle();

	for (;;) {
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		ultrawidelock_freertos_mpsl_lock();
		s_low_priority_process();
		ultrawidelock_freertos_mpsl_unlock();
	}
}

int ultrawidelock_freertos_mpsl_start(void (*low_priority_process)(void))
{
	TaskHandle_t task;

	if (low_priority_process == NULL) {
		return -1;
	}
	if (s_low_priority_process != NULL) {
		return s_low_priority_process == low_priority_process ? 0 : -1;
	}

	s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_storage);
	if (s_lock == NULL) {
		return -1;
	}
	s_low_priority_process = low_priority_process;
	task = xTaskCreateStatic(mpsl_task, "mpsl", MPSL_STACK_WORDS, NULL,
				 ULTRAWIDELOCK_FREERTOS_MPSL_TASK_PRIORITY, s_stack, &s_task_storage);
	if (task == NULL) {
		s_low_priority_process = NULL;
		s_lock = NULL;
		return -1;
	}
	s_task = task;
	return 0;
}
