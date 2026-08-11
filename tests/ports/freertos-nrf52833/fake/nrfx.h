#ifndef TEST_NRFX_H
#define TEST_NRFX_H

#include <stdint.h>

typedef int32_t IRQn_Type;

/* nRF52833 vector numbers used by the frozen peripherals.yml ownership map. */
#define POWER_CLOCK_IRQn ((IRQn_Type)0)
#define RADIO_IRQn       ((IRQn_Type)1)
#define TIMER0_IRQn      ((IRQn_Type)8)
#define RTC0_IRQn        ((IRQn_Type)11)
#define SWI5_EGU5_IRQn   ((IRQn_Type)25)
#define RTC2_IRQn        ((IRQn_Type)36)

void fake_nvic_disable_irq(IRQn_Type irq);
void fake_nvic_enable_irq(IRQn_Type irq);
void fake_nvic_set_pending_irq(IRQn_Type irq);
void fake_nvic_clear_pending_irq(IRQn_Type irq);
uint32_t fake_nvic_get_enable_irq(IRQn_Type irq);
void fake_nvic_set_priority(IRQn_Type irq, uint32_t priority);
uint32_t fake_nvic_get_priority(IRQn_Type irq);

#define NVIC_DisableIRQ(irq)          fake_nvic_disable_irq(irq)
#define NVIC_EnableIRQ(irq)           fake_nvic_enable_irq(irq)
#define NVIC_SetPendingIRQ(irq)       fake_nvic_set_pending_irq(irq)
#define NVIC_ClearPendingIRQ(irq)     fake_nvic_clear_pending_irq(irq)
#define NVIC_GetEnableIRQ(irq)        fake_nvic_get_enable_irq(irq)
#define NVIC_SetPriority(irq, priority) fake_nvic_set_priority((irq), (priority))
#define NVIC_GetPriority(irq)         fake_nvic_get_priority(irq)

/*
 * IPSR. Zero is thread mode; anything else is an active exception. The model
 * lets a test stand in either context, which is the whole question the
 * OpenThread kernel shim asks before it signals a semaphore.
 */
uint32_t fake_ipsr_get(void);
void fake_ipsr_set(uint32_t value);

#define __get_IPSR() fake_ipsr_get()

/* System reset, recorded rather than performed. */
unsigned fake_system_reset_count(void);
void fake_system_reset(void);
/* Set to leave a fatal path the way the hardware would: by never returning. */
extern void (*fake_system_reset_hook)(void);

#define NVIC_SystemReset() fake_system_reset()

/*
 * PRIMASK. The hardware-task state machine has to exclude every other context,
 * not just this driver's vector, so the model records the depth as well as the
 * bit: a test can then prove the mask is left exactly as it was found.
 */
uint32_t fake_primask_get(void);
void fake_primask_set(uint32_t value);
void fake_primask_disable_irq(void);
unsigned fake_primask_disable_count(void);
void fake_primask_reset(void);

/* Data synchronization barrier, counted so the reset path can be checked for it. */
void fake_dsb(void);
unsigned fake_dsb_count(void);

#define __DSB() fake_dsb()

#define __get_PRIMASK()    fake_primask_get()
#define __set_PRIMASK(v)   fake_primask_set(v)
#define __disable_irq()    fake_primask_disable_irq()

#endif /* TEST_NRFX_H */
