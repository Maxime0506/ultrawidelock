/**
 * @file woz_port.h
 * Portable platform shim: allocates memory, measures uptime and cycle counts, provides sleep stubs
 * for host tests, and wraps mutexes (no-op on single-threaded host).
 */
/*
 * woz_port.h - the platform contract for the UWB engine and the Aliro reader.
 * This header IS the port specification: a new target is a new branch here plus
 * a DW3000 SPI/GPIO backend, nothing else. Work queues, timers and init hooks
 * are deliberately absent (Zephyr-only diagnostics use them, never the ranging
 * path); the mutex stays a plain blocking lock so every backend is three lines.
 *
 *   woz_malloc/woz_calloc/woz_free  heap
 *   woz_uptime_us / woz_uptime_ms   monotonic time since boot
 *   woz_sleep_ms                    relinquish the CPU for at least ms
 *   woz_sleep_us                    short busy-wait, microseconds (deca_sleep)
 *   woz_cycle_get_32                free-running counter, RX-arm latency probe
 *   woz_mutex_init/lock/unlock      blocking mutex (Aliro reader trust store)
 */
#ifndef WOZ_PORT_H
#define WOZ_PORT_H

#include <stddef.h>
#include <stdint.h>

#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

static inline void *woz_malloc(size_t size)
{
	return k_malloc(size);
}
static inline void *woz_calloc(size_t n, size_t size)
{
	return k_calloc(n, size);
}
static inline void woz_free(void *ptr)
{
	k_free(ptr);
}
static inline int64_t woz_uptime_us(void)
{
	return (int64_t)k_ticks_to_us_floor64(k_uptime_ticks());
}
static inline int64_t woz_uptime_ms(void)
{
	return k_uptime_get();
}
static inline void woz_sleep_ms(int32_t ms)
{
	k_msleep(ms);
}
static inline void woz_sleep_us(int64_t us)
{
	k_usleep((int32_t)us);
}
static inline uint32_t woz_cycle_get_32(void)
{
	return k_cycle_get_32();
}
typedef struct k_mutex woz_mutex_t;
static inline void woz_mutex_init(woz_mutex_t *m)
{
	k_mutex_init(m);
}
static inline void woz_mutex_lock(woz_mutex_t *m)
{
	k_mutex_lock(m, K_FOREVER);
}
static inline void woz_mutex_unlock(woz_mutex_t *m)
{
	k_mutex_unlock(m);
}

#elif defined(ESP_PLATFORM)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>

#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static inline void *woz_malloc(size_t size)
{
	return malloc(size);
}
static inline void *woz_calloc(size_t n, size_t size)
{
	return calloc(n, size);
}
static inline void woz_free(void *ptr)
{
	free(ptr);
}
static inline int64_t woz_uptime_us(void)
{
	return esp_timer_get_time();
}
static inline int64_t woz_uptime_ms(void)
{
	return esp_timer_get_time() / 1000;
}
static inline void woz_sleep_ms(int32_t ms)
{
	if (ms > 0) {
		vTaskDelay(pdMS_TO_TICKS(ms));
	}
}
static inline void woz_sleep_us(int64_t us)
{
	esp_rom_delay_us((uint32_t)us);
}
static inline uint32_t woz_cycle_get_32(void)
{
	return esp_cpu_get_cycle_count();
}
typedef struct {
	StaticSemaphore_t buf;
	SemaphoreHandle_t h;
} woz_mutex_t;
static inline void woz_mutex_init(woz_mutex_t *m)
{
	m->h = xSemaphoreCreateMutexStatic(&m->buf);
}
static inline void woz_mutex_lock(woz_mutex_t *m)
{
	xSemaphoreTake(m->h, portMAX_DELAY);
}
static inline void woz_mutex_unlock(woz_mutex_t *m)
{
	xSemaphoreGive(m->h);
}

#elif defined(WOZ_PORT_HOST)

#include <stdlib.h>
#include <time.h>

/**
 * @brief Allocate size bytes.
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
static inline void *woz_malloc(size_t size)
{
	return malloc(size);
}
/**
 * @brief Allocate and zero-initialize n elements of size bytes each.
 * @param n Number of elements.
 * @param size Bytes per element.
 * @return Pointer to allocated and zeroed memory, or NULL on failure.
 */
static inline void *woz_calloc(size_t n, size_t size)
{
	return calloc(n, size);
}
/**
 * @brief Deallocate memory.
 * @param ptr Pointer to memory to free (may be NULL).
 */
static inline void woz_free(void *ptr)
{
	free(ptr);
}
/**
 * @brief Monotonic microseconds since boot.
 * @return Microseconds elapsed since system start.
 */
static inline int64_t woz_uptime_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
/**
 * @brief Monotonic milliseconds since boot.
 * @return Milliseconds elapsed since system start.
 */
static inline int64_t woz_uptime_ms(void)
{
	return woz_uptime_us() / 1000;
}
/**
 * @brief Sleep for a given number of milliseconds (host-test stub).
 * @param ms milliseconds to sleep; ignored in deterministic host tests.
 */
static inline void woz_sleep_ms(int32_t ms)
{
	(void)ms; /* host tests are deterministic; nothing to wait for */
}
/**
 * @brief Sleep for a given number of microseconds (host-test stub).
 * @param us microseconds to sleep; ignored in deterministic host tests.
 */
static inline void woz_sleep_us(int64_t us)
{
	(void)us;
}
/**
 * @brief Retrieve a 32-bit cycle counter with microsecond resolution.
 * @return current uptime in microseconds, cast to uint32_t.
 */
static inline uint32_t woz_cycle_get_32(void)
{
	return (uint32_t)woz_uptime_us(); /* us resolution is plenty for the probe */
}
/**
 * @brief Opaque mutex type for host tests (single-threaded, no-op).
 */
typedef int woz_mutex_t;
/**
 * @brief Initialize a mutex (host-test stub).
 * @param m pointer to mutex to initialize; no-op in single-threaded tests.
 */
static inline void woz_mutex_init(woz_mutex_t *m)
{
	(void)m;
}
/**
 * @brief Acquire a mutex (host-test stub).
 * @param m pointer to mutex to lock; no-op in single-threaded tests.
 */
static inline void woz_mutex_lock(woz_mutex_t *m)
{
	(void)m;
}
/**
 * @brief Release a mutex (host-test stub).
 * @param m pointer to mutex to unlock; no-op in single-threaded tests.
 */
static inline void woz_mutex_unlock(woz_mutex_t *m)
{
	(void)m;
}

#else
#error "woz_port.h: no platform backend. Define WOZ_PORT_HOST, or build under Zephyr/ESP-IDF."
#endif

#endif /* WOZ_PORT_H */
