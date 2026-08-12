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
/*
 * The FromISR ceiling, carried over from board/FreeRTOSConfig.h so the static
 * assertions that depend on it -- radio/radio_start_freertos.c's and
 * uwb/dw3000_hw_freertos.c's -- are live in the host suite instead of waiting
 * for those layers to join the target build graph. If the two files ever
 * disagree the host suite is checking the wrong number, so this is a duplicate
 * on purpose and not a default.
 *
 * Four is bounded on both sides. It must be at most 4 for MPSL's low-priority
 * handler on SWI5_EGU5, which calls a FreeRTOS FromISR API from priority 4; and
 * it must be above 1 so MPSL's own priority-0 handlers and the 802.15.4 SWI at
 * 1 sit above the ceiling and are never masked by a scheduler critical section.
 * The DW3110's GPIOTE vector takes the same 4, which is the most urgent level
 * at which it may still notify its worker task.
 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 4
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
