// Memory allocation and timing facade: qmalloc, qcalloc, qfree wrap the platform heap;
// qrtc_get_us returns monotonic microseconds since boot.
/*
 * ultrawidelock_alloc.h - thin heap + monotonic-clock wrappers over woz_port.h.
 */
#ifndef WOZ_ALLOC_H
#define WOZ_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#include "woz_port.h"

/**
 * @brief Allocate size bytes.
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
static inline void *qmalloc(size_t size)
{
	return woz_malloc(size);
}

/**
 * @brief Allocate and zero-initialize nb_items elements of item_size bytes each.
 * @param nb_items Number of items.
 * @param item_size Bytes per item.
 * @return Pointer to allocated and zeroed memory, or NULL on failure.
 */
static inline void *qcalloc(size_t nb_items, size_t item_size)
{
	return woz_calloc(nb_items, item_size);
}

/**
 * @brief Deallocate memory previously allocated by qmalloc or qcalloc.
 * @param ptr Pointer to memory to free (may be NULL).
 */
static inline void qfree(void *ptr)
{
	woz_free(ptr);
}

/**
 * @brief Monotonic microseconds since boot.
 * @return Microseconds elapsed since system start.
 */
static inline int64_t qrtc_get_us(void)
{
	return woz_uptime_us();
}

#endif /* WOZ_ALLOC_H */
