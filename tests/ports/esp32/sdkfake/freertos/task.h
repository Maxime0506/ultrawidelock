/* sdkfake freertos/task.h */
#ifndef SDKFAKE_FREERTOS_TASK_H
#define SDKFAKE_FREERTOS_TASK_H

#include "FreeRTOS.h"

typedef struct fake_task *TaskHandle_t;

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
		       UBaseType_t prio, TaskHandle_t *out);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
				   UBaseType_t prio, TaskHandle_t *out, int core);
void vTaskDelay(TickType_t ticks);

#endif
