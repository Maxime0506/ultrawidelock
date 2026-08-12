/*
 * The nrfx integration glue for this port.
 *
 * nrfx expects the host environment to supply assertions, interrupt control, a
 * critical section, a microsecond delay, and atomics. Two of those are worth
 * reading closely rather than skimming, because getting them wrong here would
 * quietly undo the interrupt policy the rest of the port is built on.
 *
 * The critical section is PRIMASK, not BASEPRI. That is deliberate and it is
 * the opposite of what a FreeRTOS integration would normally do: masking with
 * BASEPRI at configMAX_SYSCALL_INTERRUPT_PRIORITY would leave MPSL at priority
 * 0 and the nRF 802.15.4 driver at 1 free to run inside it, which is exactly
 * what a register read-modify-write on a shared peripheral must not allow. The
 * regions nrfx opens are a handful of instructions long, so masking everything
 * for them costs the radio nothing measurable.
 *
 * The delay is the port's own busy wait rather than nrfx's cycle-counting one,
 * so that a delay taken from a driver measures against the same RTC1 timebase
 * as every other wait in the image.
 */
#ifndef WOZ_FREERTOS_NRFX_GLUE_H
#define WOZ_FREERTOS_NRFX_GLUE_H

#include <stdbool.h>
#include <stdint.h>

#include <nrf.h>

#include <woz_freertos_platform.h>

#define NRFX_ASSERT(expression)                                                                    \
	do {                                                                                       \
		if (!(expression)) {                                                               \
			woz_freertos_fatal("nrfx assertion failed");                               \
		}                                                                                  \
	} while (0)

#define NRFX_STATIC_ASSERT(expression) _Static_assert(expression, "nrfx static assertion failed")

#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) NVIC_SetPriority(irq_number, priority)
#define NRFX_IRQ_ENABLE(irq_number) NVIC_EnableIRQ(irq_number)
#define NRFX_IRQ_IS_ENABLED(irq_number) (0 != (NVIC->ISER[(irq_number) / 32] & (1UL << ((irq_number) % 32))))
#define NRFX_IRQ_DISABLE(irq_number) NVIC_DisableIRQ(irq_number)
#define NRFX_IRQ_PENDING_SET(irq_number) NVIC_SetPendingIRQ(irq_number)
#define NRFX_IRQ_PENDING_CLEAR(irq_number) NVIC_ClearPendingIRQ(irq_number)
#define NRFX_IRQ_IS_PENDING(irq_number) (NVIC_GetPendingIRQ(irq_number) == 1)

/*
 * Nesting is handled by saving PRIMASK rather than by counting, so an inner
 * region cannot re-enable interrupts an outer one had masked.
 */
#define NRFX_CRITICAL_SECTION_ENTER()                                                            \
	{                                                                                          \
		uint32_t _woz_primask = __get_PRIMASK();                                           \
		__disable_irq();

#define NRFX_CRITICAL_SECTION_EXIT()                                                               \
		__set_PRIMASK(_woz_primask);                                                       \
	}

#define NRFX_DELAY_US(us_time) woz_freertos_busy_wait_us((uint64_t)(us_time))

typedef uint32_t nrfx_atomic_t;

/*
 * The atomics are exclusive-access loops rather than mask-and-write, because
 * nrfx uses them for the hardware-resource bitmasks that MPSL and the 802.15.4
 * driver also touch, and those two run at interrupt priorities this port must
 * never mask for longer than a few instructions.
 */
#define WOZ_NRFX_ATOMIC_OP(p_data, expr)                                                           \
	({                                                                                         \
		uint32_t _woz_old;                                                                 \
		uint32_t _woz_new;                                                                 \
		do {                                                                               \
			_woz_old = __LDREXW((volatile uint32_t *)(p_data));                        \
			_woz_new = (expr);                                                         \
		} while (__STREXW(_woz_new, (volatile uint32_t *)(p_data)) != 0u);                 \
		__DMB();                                                                           \
		_woz_old;                                                                          \
	})

#define NRFX_ATOMIC_FETCH_STORE(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, (uint32_t)(value))
#define NRFX_ATOMIC_FETCH_OR(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, _woz_old | (uint32_t)(value))
#define NRFX_ATOMIC_FETCH_AND(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, _woz_old & (uint32_t)(value))
#define NRFX_ATOMIC_FETCH_XOR(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, _woz_old ^ (uint32_t)(value))
#define NRFX_ATOMIC_FETCH_ADD(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, _woz_old + (uint32_t)(value))
#define NRFX_ATOMIC_FETCH_SUB(p_data, value) WOZ_NRFX_ATOMIC_OP(p_data, _woz_old - (uint32_t)(value))

static inline bool woz_freertos_nrfx_atomic_cas(nrfx_atomic_t *p_data, uint32_t old_value,
						uint32_t new_value)
{
	if (__LDREXW((volatile uint32_t *)p_data) != old_value) {
		__CLREX();
		return false;
	}
	if (__STREXW(new_value, (volatile uint32_t *)p_data) != 0u) {
		return false;
	}
	__DMB();
	return true;
}

#define NRFX_ATOMIC_CAS(p_data, old_value, new_value)                                              \
	woz_freertos_nrfx_atomic_cas(p_data, (uint32_t)(old_value), (uint32_t)(new_value))

#define NRFX_CLZ(value) __CLZ(value)
#define NRFX_CTZ(value) (__CLZ(__RBIT(value)))

#define NRFX_EVENT_READBACK_ENABLED 1

#endif /* WOZ_FREERTOS_NRFX_GLUE_H */
