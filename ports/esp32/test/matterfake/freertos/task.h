/* matterfake freertos/task.h — task + notification fakes (matterfake.cc). */
#ifndef MATTERFAKE_FREERTOS_TASK_H
#define MATTERFAKE_FREERTOS_TASK_H

#include "FreeRTOS.h"

typedef struct mfk_task_opaque *TaskHandle_t;

#ifdef __cplusplus
extern "C" {
#endif

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
		       UBaseType_t prio, TaskHandle_t *out);
void vTaskDelay(TickType_t ticks);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);

#ifdef __cplusplus
}
#endif

#endif /* MATTERFAKE_FREERTOS_TASK_H */
