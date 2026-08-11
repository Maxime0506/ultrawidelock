#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;

#define pdFALSE 0
#define pdTRUE  1
#define pdPASS  1
#define pdFAIL  0

#define portMAX_DELAY      UINT32_MAX
#define portTICK_PERIOD_MS 1u
/* A 1 kHz tick, matching portTICK_PERIOD_MS above. */
#define configTICK_RATE_HZ 1000u
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
#define tskIDLE_PRIORITY   0u

#define configSUPPORT_STATIC_ALLOCATION    1
#define configSUPPORT_DYNAMIC_ALLOCATION   1
#define configUSE_MUTEXES                  1
#define configUSE_RECURSIVE_MUTEXES        1
#define configUSE_COUNTING_SEMAPHORES      1
#define configUSE_TASK_NOTIFICATIONS        1
/* NimBLE callouts are FreeRTOS software timers. */
#define configUSE_TIMERS                   1
#define configMAX_PRIORITIES               8
#define configASSERT(condition)             assert(condition)

typedef struct {
	unsigned count;
	unsigned limit;
	unsigned takes;
	unsigned gives;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;

typedef struct {
	uint8_t *storage;
	UBaseType_t depth;
	UBaseType_t item_size;
	UBaseType_t head;
	UBaseType_t tail;
	UBaseType_t count;
} StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;

typedef struct {
	unsigned created;
	unsigned notifications;
} StaticTask_t;
typedef StaticTask_t *TaskHandle_t;

void fake_port_yield_from_isr(BaseType_t wake);
#define portYIELD_FROM_ISR(wake) fake_port_yield_from_isr(wake)

void *pvPortMalloc(size_t size);
void vPortFree(void *ptr);

#endif /* TEST_FREERTOS_H */
