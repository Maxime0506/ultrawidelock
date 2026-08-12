#ifndef TEST_FREERTOS_QUEUE_H
#define TEST_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

QueueHandle_t xQueueCreateStatic(UBaseType_t depth, UBaseType_t item_size, uint8_t *storage,
				 StaticQueue_t *queue_storage);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);

#endif /* TEST_FREERTOS_QUEUE_H */
