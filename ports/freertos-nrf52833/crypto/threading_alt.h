/*
 * The mutex type Mbed TLS uses when MBEDTLS_THREADING_ALT is set.
 *
 * The name of this file is not a choice: include/mbedtls/threading.h does
 * #include "threading_alt.h" verbatim, so the directory holding it has to be
 * on the include path of every translation unit that reaches Mbed TLS.
 *
 * The storage is embedded rather than pointed at. Mbed TLS declares its global
 * mutexes as file-scope objects and hands their addresses to the init
 * callback, which has no return value and so cannot report an allocation
 * failure. A static FreeRTOS mutex has nothing to fail.
 */
#ifndef WOZ_FREERTOS_THREADING_ALT_H
#define WOZ_FREERTOS_THREADING_ALT_H

#include <FreeRTOS.h>
#include <semphr.h>

typedef struct {
	StaticSemaphore_t storage;
	SemaphoreHandle_t handle;
	/*
	 * Mbed TLS requires that a mutex which failed to initialise, or which
	 * was freed, makes every subsequent lock return an error rather than
	 * succeeding by accident. Zeroed static storage gives handle == NULL,
	 * so an unlocked-before-init mutex is already in that state.
	 */
} mbedtls_threading_mutex_t;

#endif /* WOZ_FREERTOS_THREADING_ALT_H */
