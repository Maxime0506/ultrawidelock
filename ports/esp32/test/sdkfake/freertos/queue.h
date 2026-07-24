/* sdkfake freertos/queue.h — one-slot copying queue; receive is hookable so a
 * test can pump a worker task's loop synchronously and then break out. */
#ifndef SDKFAKE_FREERTOS_QUEUE_H
#define SDKFAKE_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

typedef struct fake_queue *QueueHandle_t;
struct fake_queue {
	size_t item_size;
	int full;
	uint8_t item[4096];
};

QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);

extern int fake_queue_create_rc; /* 1 = ok (default); 0 = return NULL */
/* Called when a receive on an EMPTY queue would block forever (worker pump). */
extern void (*fake_queue_block_hook)(void);

#endif
