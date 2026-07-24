/* sdkfake freertos/semphr.h — mutexes always succeed; the binary semaphore's
 * take is hookable so a test can pump an ISR-service loop synchronously. */
#ifndef SDKFAKE_FREERTOS_SEMPHR_H
#define SDKFAKE_FREERTOS_SEMPHR_H

#include "FreeRTOS.h"

typedef struct fake_sem *SemaphoreHandle_t;
struct fake_sem {
	int is_mutex;
	int count;     /* binary semaphore pending gives */
	int takes;
	int gives;
	int gives_isr;
};

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken);

/* Optional: overrides xSemaphoreTake for binary semaphores (ISR-loop pump). */
extern BaseType_t (*fake_sem_take_hook)(SemaphoreHandle_t s, TickType_t ticks);

#endif
