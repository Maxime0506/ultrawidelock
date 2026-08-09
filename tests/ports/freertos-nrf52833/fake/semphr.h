#ifndef TEST_FREERTOS_SEMPHR_H
#define TEST_FREERTOS_SEMPHR_H

#include "FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage);
SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t limit, UBaseType_t initial,
						 StaticSemaphore_t *storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);

#endif /* TEST_FREERTOS_SEMPHR_H */
