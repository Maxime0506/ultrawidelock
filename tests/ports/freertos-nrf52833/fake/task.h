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

#endif /* TEST_FREERTOS_TASK_H */
