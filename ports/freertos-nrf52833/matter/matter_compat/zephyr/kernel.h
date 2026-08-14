/*
 * The one kernel service the shared Matter Thread transport uses.
 *
 * k_msleep appears once, polling the Thread role while waiting to attach. It
 * runs on a task, never in an interrupt, so a plain vTaskDelay is the whole
 * translation.
 */
#ifndef WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H
#define WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H

#include <FreeRTOS.h>
#include <task.h>

#define ARG_UNUSED(x) ((void)(x))

static inline void k_msleep(uint32_t ms)
{
	vTaskDelay(pdMS_TO_TICKS(ms));
}

#endif /* WOZ_MATTER_COMPAT_ZEPHYR_KERNEL_H */
