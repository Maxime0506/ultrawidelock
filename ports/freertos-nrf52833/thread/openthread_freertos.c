#include "ultrawidelock_freertos_openthread.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <openthread/tasklet.h>

#if configSUPPORT_STATIC_ALLOCATION != 1
#error "The OpenThread task requires configSUPPORT_STATIC_ALLOCATION=1"
#endif
#if configUSE_RECURSIVE_MUTEXES != 1
#error "OpenThread API serialization requires configUSE_RECURSIVE_MUTEXES=1"
#endif
#if configUSE_TASK_NOTIFICATIONS != 1
#error "The OpenThread event pump requires configUSE_TASK_NOTIFICATIONS=1"
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_OPENTHREAD_STACK_BYTES
#define ULTRAWIDELOCK_FREERTOS_OPENTHREAD_STACK_BYTES 4096u
#endif

#ifndef ULTRAWIDELOCK_FREERTOS_OPENTHREAD_TASK_PRIORITY
#define ULTRAWIDELOCK_FREERTOS_OPENTHREAD_TASK_PRIORITY (tskIDLE_PRIORITY + 1u)
#endif

#define OT_STACK_WORDS                                                                    \
	((ULTRAWIDELOCK_FREERTOS_OPENTHREAD_STACK_BYTES + sizeof(StackType_t) - 1u) / sizeof(StackType_t))

static otInstance *s_instance;
static TaskHandle_t s_task;
static StaticTask_t s_task_storage;
static StackType_t s_stack[OT_STACK_WORDS];
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;

void ultrawidelock_freertos_openthread_lock(void)
{
	BaseType_t taken;

	configASSERT(s_lock != NULL);
	taken = xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
	configASSERT(taken == pdTRUE);
	(void)taken;
}

void ultrawidelock_freertos_openthread_unlock(void)
{
	BaseType_t given;

	configASSERT(s_lock != NULL);
	given = xSemaphoreGiveRecursive(s_lock);
	configASSERT(given == pdTRUE);
	(void)given;
}

void ultrawidelock_freertos_openthread_wake(void)
{
	TaskHandle_t task = s_task;

	if (task != NULL) {
		xTaskNotifyGive(task);
	}
}

void ultrawidelock_freertos_openthread_wake_from_isr(void)
{
	BaseType_t wake = pdFALSE;
	TaskHandle_t task = s_task;

	if (task != NULL) {
		vTaskNotifyGiveFromISR(task, &wake);
		portYIELD_FROM_ISR(wake);
	}
}

/* Required by the upstream OpenThread tasklet platform contract. */
void otTaskletsSignalPending(otInstance *instance)
{
	if (instance == s_instance) {
		ultrawidelock_freertos_openthread_wake();
	}
}

static void openthread_task(void *arg)
{
	otInstance *instance = arg;

	/* xTaskCreateStatic() can schedule this task before it returns to start(). */
	s_task = xTaskGetCurrentTaskHandle();
	for (;;) {
		ultrawidelock_freertos_openthread_lock();
		do {
			otTaskletsProcess(instance);
			ultrawidelock_freertos_openthread_process_drivers(instance);
		} while (otTaskletsArePending(instance));
		ultrawidelock_freertos_openthread_unlock();

		/* Radio and alarm ISRs, driver callbacks, and tasklet signalling all
		 * retain a notification if they race with this transition. */
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}
}

int ultrawidelock_freertos_openthread_start(otInstance *instance)
{
	TaskHandle_t task;

	if (instance == NULL) {
		return -1;
	}
	if (s_instance != NULL) {
		return s_instance == instance ? 0 : -1;
	}

	s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_storage);
	if (s_lock == NULL) {
		return -1;
	}

	s_instance = instance;
	task = xTaskCreateStatic(openthread_task, "openthread", OT_STACK_WORDS, instance,
				 ULTRAWIDELOCK_FREERTOS_OPENTHREAD_TASK_PRIORITY, s_stack, &s_task_storage);
	if (task == NULL) {
		s_instance = NULL;
		s_lock = NULL;
		return -1;
	}
	s_task = task;
	return 0;
}

otInstance *ultrawidelock_freertos_openthread_instance(void)
{
	return s_instance;
}
