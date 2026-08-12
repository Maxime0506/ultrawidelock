#include <stdlib.h>
#include <string.h>

#include "fake_freertos.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

int64_t fake_uptime_us;
unsigned fake_malloc_calls;
unsigned fake_free_calls;
unsigned fake_delay_calls;
TickType_t fake_delay_ticks;
unsigned fake_task_count;
unsigned fake_task_create_failures;
void (*fake_task_entry)(void *);
void *fake_task_arg;
UBaseType_t fake_task_priority;
uint32_t fake_task_stack_depth;
void (*fake_queue_block_hook)(void);
void (*fake_notify_block_hook)(void);
unsigned fake_task_notify_calls;
unsigned fake_isr_notify_calls;
unsigned fake_isr_yield_calls;
unsigned fake_recursive_take_calls;
unsigned fake_recursive_give_calls;
static TaskHandle_t fake_current_task;

void fake_freertos_reset(void)
{
	fake_uptime_us = 0;
	fake_malloc_calls = 0;
	fake_free_calls = 0;
	fake_delay_calls = 0;
	fake_delay_ticks = 0;
	fake_task_count = 0;
	fake_task_create_failures = 0;
	fake_task_entry = NULL;
	fake_task_arg = NULL;
	fake_task_priority = 0;
	fake_task_stack_depth = 0;
	fake_queue_block_hook = NULL;
	fake_notify_block_hook = NULL;
	fake_task_notify_calls = 0;
	fake_isr_notify_calls = 0;
	fake_isr_yield_calls = 0;
	fake_recursive_take_calls = 0;
	fake_recursive_give_calls = 0;
	fake_current_task = NULL;
	fake_semaphore_isr_gives = 0;
}

void *pvPortMalloc(size_t size)
{
	void *block;

	fake_malloc_calls++;

	block = malloc(size);
	if (block != NULL) {
		/*
		 * Poisoned, because the FreeRTOS heap does not zero and hands
		 * back recycled memory. A fresh malloc from the host allocator
		 * usually arrives zeroed, which quietly makes any caller that
		 * forgot to initialise its block pass here and fail on the
		 * board.
		 */
		memset(block, 0x5a, size);
	}
	return block;
}

void vPortFree(void *ptr)
{
	fake_free_calls++;
	free(ptr);
}

void vTaskDelay(TickType_t ticks)
{
	fake_delay_calls++;
	fake_delay_ticks += ticks;
	fake_uptime_us += (int64_t)ticks * portTICK_PERIOD_MS * 1000;
	/*
	 * The tick count moves with the delay. Without that a caller polling a
	 * deadline would spin forever here while passing on hardware, which is
	 * the wrong way round for a timeout to be tested.
	 */
	fake_task_advance_tick_count(ticks);
}

TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t stack_depth,
			       void *arg, UBaseType_t priority, StackType_t *stack,
			       StaticTask_t *task_storage)
{
	(void)name;
	if (entry == NULL || stack == NULL || task_storage == NULL) {
		return NULL;
	}
	if (fake_task_create_failures > 0) {
		fake_task_create_failures--;
		return NULL;
	}
	task_storage->created = 1;
	task_storage->notifications = 0;
	fake_task_count++;
	fake_task_entry = entry;
	fake_task_arg = arg;
	fake_task_priority = priority;
	fake_task_stack_depth = stack_depth;
	fake_current_task = task_storage;
	return task_storage;
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
	return xSemaphoreCreateCountingStatic(1, 1, storage);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage)
{
	return xSemaphoreCreateMutexStatic(storage);
}

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t limit, UBaseType_t initial,
						 StaticSemaphore_t *storage)
{
	if (storage == NULL || limit == 0 || initial > limit) {
		return NULL;
	}
	memset(storage, 0, sizeof(*storage));
	storage->count = initial;
	storage->limit = limit;
	return storage;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
	(void)ticks;
	if (sem == NULL) {
		return pdFALSE;
	}
	sem->takes++;
	if (sem->count == 0) {
		return pdFALSE;
	}
	sem->count--;
	return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
	if (sem == NULL) {
		return pdFALSE;
	}
	sem->gives++;
	if (sem->count == sem->limit) {
		return pdFALSE;
	}
	sem->count++;
	return pdTRUE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks)
{
	fake_recursive_take_calls++;
	return xSemaphoreTake(sem, ticks);
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem)
{
	fake_recursive_give_calls++;
	return xSemaphoreGive(sem);
}

unsigned fake_semaphore_isr_gives;

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *wake)
{
	fake_semaphore_isr_gives++;
	/*
	 * Reports a waiter released, which is the case worth modelling: the
	 * caller must then yield, or the woken task waits out the tick.
	 */
	if (wake != NULL) {
		*wake = pdTRUE;
	}
	return xSemaphoreGive(sem);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	return fake_current_task;
}

BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
	if (task == NULL) {
		return pdFAIL;
	}
	task->notifications++;
	fake_task_notify_calls++;
	return pdPASS;
}

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher_priority_task_woken)
{
	if (task == NULL) {
		return;
	}
	task->notifications++;
	fake_isr_notify_calls++;
	if (higher_priority_task_woken != NULL) {
		*higher_priority_task_woken = pdTRUE;
	}
}

uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	uint32_t count = task->notifications;

	if (count != 0u) {
		task->notifications = clear_count_on_exit ? 0u : count - 1u;
		return count;
	}
	if (ticks == portMAX_DELAY && fake_notify_block_hook != NULL) {
		fake_notify_block_hook();
	}
	return 0u;
}

void fake_port_yield_from_isr(BaseType_t wake)
{
	if (wake == pdTRUE) {
		fake_isr_yield_calls++;
	}
}

QueueHandle_t xQueueCreateStatic(UBaseType_t depth, UBaseType_t item_size, uint8_t *storage,
				 StaticQueue_t *queue_storage)
{
	if (depth == 0 || item_size == 0 || storage == NULL || queue_storage == NULL) {
		return NULL;
	}
	memset(queue_storage, 0, sizeof(*queue_storage));
	queue_storage->storage = storage;
	queue_storage->depth = depth;
	queue_storage->item_size = item_size;
	return queue_storage;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
	(void)ticks;
	if (queue == NULL || item == NULL || queue->count == queue->depth) {
		return pdFALSE;
	}
	memcpy(queue->storage + queue->tail * queue->item_size, item, queue->item_size);
	queue->tail = (queue->tail + 1) % queue->depth;
	queue->count++;
	return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
	if (queue == NULL || item == NULL) {
		return pdFALSE;
	}
	if (queue->count != 0) {
		memcpy(item, queue->storage + queue->head * queue->item_size, queue->item_size);
		queue->head = (queue->head + 1) % queue->depth;
		queue->count--;
		return pdTRUE;
	}
	if (ticks != 0 && ticks != portMAX_DELAY) {
		fake_uptime_us += (int64_t)ticks * portTICK_PERIOD_MS * 1000;
	}
	if (ticks == portMAX_DELAY && fake_queue_block_hook != NULL) {
		fake_queue_block_hook();
	}
	return pdFALSE;
}

/*
 * The tick count. The board's uptime hook extends it past 32 bits, and the only
 * way to reach that extension in a bounded run is to place the count just short
 * of the wrap, so the model lets a test set it outright.
 */
static TickType_t s_tick_count;

TickType_t xTaskGetTickCount(void)
{
	return s_tick_count;
}

TickType_t xTaskGetTickCountFromISR(void)
{
	return s_tick_count;
}

void fake_task_set_tick_count(TickType_t ticks)
{
	s_tick_count = ticks;
}

void fake_task_advance_tick_count(TickType_t ticks)
{
	s_tick_count += ticks;
}
