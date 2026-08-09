#ifndef TEST_NRFX_H
#define TEST_NRFX_H

#include <stdint.h>

typedef int32_t IRQn_Type;

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

#endif /* TEST_NRFX_H */
