/*
 * The Zephyr kernel objects the pinned OpenThread radio platform uses, on
 * FreeRTOS. See ot_compat/woz_freertos_ot_kernel.h for the surface and why it
 * is only this much.
 *
 * Every operation here is reachable from an 802.15.4 driver callout, which
 * runs in interrupt context, as well as from the OpenThread task. That is why
 * the queue and the atomics mask interrupts rather than take a lock, and why
 * the semaphore has to pick its FreeRTOS entry point by context.
 */
#include <woz_freertos_ot_kernel.h>

#include <woz_freertos_platform.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <nrfx.h>

/*
 * Cortex-M reports the active exception number in IPSR, and zero means thread
 * mode. FreeRTOS refuses its task-context calls from an interrupt and its ISR
 * calls from a task, so the semaphore has to ask before it signals.
 */
static bool in_isr(void)
{
	return __get_IPSR() != 0u;
}

void k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit)
{
	sem->handle = xSemaphoreCreateCountingStatic(limit, initial_count, &sem->storage);
	if (sem->handle == NULL) {
		woz_freertos_fatal("openthread semaphore");
	}
}

void k_sem_give(struct k_sem *sem)
{
	if (in_isr()) {
		BaseType_t wake = pdFALSE;

		(void)xSemaphoreGiveFromISR(sem->handle, &wake);
		portYIELD_FROM_ISR(wake);
		return;
	}
	(void)xSemaphoreGive(sem->handle);
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	if (xSemaphoreTake(sem->handle, timeout.ticks) == pdTRUE) {
		return 0;
	}
	/* Zephyr reports a timeout, and only K_NO_WAIT can produce one here. */
	return -EAGAIN;
}

/*
 * The FIFO. Zephyr reserves the first word of every queued item for the link,
 * and the radio platform's receive frame does the same, so items are pushed by
 * address and nothing is ever allocated.
 */
static void **link_of(void *item)
{
	return (void **)item;
}

void k_fifo_init(struct k_fifo *fifo)
{
	fifo->head = NULL;
	fifo->tail = NULL;
}

void k_fifo_put(struct k_fifo *fifo, void *item)
{
	uint32_t primask = __get_PRIMASK();

	*link_of(item) = NULL;

	__disable_irq();
	if (fifo->tail == NULL) {
		fifo->head = item;
	} else {
		*link_of(fifo->tail) = item;
	}
	fifo->tail = item;
	__set_PRIMASK(primask);
}

void *k_fifo_get(struct k_fifo *fifo, k_timeout_t timeout)
{
	uint32_t primask;
	void *item;

	/*
	 * A blocking get would have to park the caller, and the only caller is
	 * the OpenThread task draining the pool without waiting. Refusing is
	 * better than a wait that silently returns immediately.
	 */
	if (timeout.ticks != 0u) {
		woz_freertos_fatal("openthread fifo blocking get");
	}

	primask = __get_PRIMASK();
	__disable_irq();
	item = fifo->head;
	if (item != NULL) {
		fifo->head = *link_of(item);
		if (fifo->head == NULL) {
			fifo->tail = NULL;
		}
	}
	__set_PRIMASK(primask);

	return item;
}

_Noreturn void k_oops(void)
{
	woz_freertos_fatal("openthread radio platform");
}

_Noreturn void woz_freertos_ot_assert_failed(const char *file, int line, const char *test)
{
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, "ot_radio", "assert %s:%d: %s", file, line, test);
	woz_freertos_fatal("openthread radio assertion");
}

/*
 * The atomics. A bit is set from a driver callout and cleared by the
 * OpenThread task after it acts on it, so a read-modify-write that an
 * interrupt can land inside would lose events.
 */
static atomic_t *word_of(atomic_t *target, int bit)
{
	return target + ((unsigned)bit / WOZ_OT_ATOMIC_BITS);
}

static atomic_t mask_of(int bit)
{
	return (atomic_t)1u << ((unsigned)bit % WOZ_OT_ATOMIC_BITS);
}

bool atomic_test_bit(const atomic_t *target, int bit)
{
	return (*word_of((atomic_t *)target, bit) & mask_of(bit)) != 0u;
}

void atomic_set_bit(atomic_t *target, int bit)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	*word_of(target, bit) |= mask_of(bit);
	__set_PRIMASK(primask);
}

void atomic_clear_bit(atomic_t *target, int bit)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	*word_of(target, bit) &= ~mask_of(bit);
	__set_PRIMASK(primask);
}
