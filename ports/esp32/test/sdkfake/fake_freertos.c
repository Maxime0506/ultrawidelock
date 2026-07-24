/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * fake_freertos — synchronous recording doubles for the few FreeRTOS calls the
 * ESP32 port sources make. Tasks are recorded, never scheduled; a test pumps a
 * recorded task function itself and uses the take/receive hooks to break out of
 * its forever-loop.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

int fake_portmux_depth;
int fake_port_yields;

struct fake_task fake_tasks[FAKE_TASK_MAX];
int fake_task_count;
int fake_task_create_rc = pdPASS;

int fake_delay_calls;
TickType_t fake_delay_total_ticks;
void (*fake_delay_hook)(void);

BaseType_t (*fake_sem_take_hook)(SemaphoreHandle_t s, TickType_t ticks);

int fake_queue_create_rc = 1;
void (*fake_queue_block_hook)(void);

static struct fake_sem s_sems[8];
static int s_sem_count;
static struct fake_queue s_queues[4];
static int s_queue_count;

void fake_freertos_reset(void)
{
	memset(fake_tasks, 0, sizeof(fake_tasks));
	fake_task_count = 0;
	fake_task_create_rc = pdPASS;
	fake_delay_calls = 0;
	fake_delay_total_ticks = 0;
	fake_delay_hook = NULL;
	fake_sem_take_hook = NULL;
	fake_queue_create_rc = 1;
	fake_queue_block_hook = NULL;
	fake_portmux_depth = 0;
	fake_port_yields = 0;
	memset(s_sems, 0, sizeof(s_sems));
	s_sem_count = 0;
	memset(s_queues, 0, sizeof(s_queues));
	s_queue_count = 0;
}

static BaseType_t task_create(void (*fn)(void *), const char *name, void *arg, UBaseType_t prio,
			      TaskHandle_t *out, int core)
{
	if (fake_task_create_rc != pdPASS || fake_task_count >= FAKE_TASK_MAX) {
		return pdFAIL;
	}

	struct fake_task *t = &fake_tasks[fake_task_count++];

	t->fn = fn;
	t->arg = arg;
	snprintf(t->name, sizeof(t->name), "%s", name ? name : "");
	t->prio = prio;
	t->core = core;
	if (out != NULL) {
		*out = t;
	}
	return pdPASS;
}

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
		       UBaseType_t prio, TaskHandle_t *out)
{
	(void)stack;
	return task_create(fn, name, arg, prio, out, -1);
}

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
				   UBaseType_t prio, TaskHandle_t *out, int core)
{
	(void)stack;
	return task_create(fn, name, arg, prio, out, core);
}

void vTaskDelay(TickType_t ticks)
{
	fake_delay_calls++;
	fake_delay_total_ticks += ticks;
	if (fake_delay_hook != NULL) {
		fake_delay_hook();
	}
}

static SemaphoreHandle_t sem_create(int is_mutex)
{
	if (s_sem_count >= (int)(sizeof(s_sems) / sizeof(s_sems[0]))) {
		return NULL;
	}

	struct fake_sem *s = &s_sems[s_sem_count++];

	memset(s, 0, sizeof(*s));
	s->is_mutex = is_mutex;
	return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	return sem_create(1);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	return sem_create(0);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
	s->takes++;
	if (!s->is_mutex && fake_sem_take_hook != NULL) {
		return fake_sem_take_hook(s, ticks);
	}
	return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
	s->gives++;
	if (!s->is_mutex) {
		s->count++;
	}
	return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
	s->gives_isr++;
	s->count++;
	if (woken != NULL) {
		*woken = pdTRUE;
	}
	return pdTRUE;
}

QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size)
{
	(void)depth; /* production uses depth 1; the fake is one slot regardless */
	if (!fake_queue_create_rc ||
	    s_queue_count >= (int)(sizeof(s_queues) / sizeof(s_queues[0])) ||
	    item_size > sizeof(s_queues[0].item)) {
		return NULL;
	}

	struct fake_queue *q = &s_queues[s_queue_count++];

	q->item_size = item_size;
	q->full = 0;
	return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
	(void)ticks;
	if (q->full) {
		return pdFALSE; /* non-blocking send on a full queue drops */
	}
	memcpy(q->item, item, q->item_size);
	q->full = 1;
	return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
	if (q->full) {
		memcpy(item, q->item, q->item_size);
		q->full = 0;
		return pdTRUE;
	}
	if (ticks == portMAX_DELAY && fake_queue_block_hook != NULL) {
		fake_queue_block_hook(); /* usually longjmps out of the pump */
	}
	return pdFALSE;
}
