#ifndef TEST_NRF_802154_IRQ_H
#define TEST_NRF_802154_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*nrf_802154_isr_t)();

void nrf_802154_irq_init(uint32_t irqn, int32_t prio, nrf_802154_isr_t isr);
void nrf_802154_irq_enable(uint32_t irqn);
void nrf_802154_irq_disable(uint32_t irqn);
void nrf_802154_irq_set_pending(uint32_t irqn);
void nrf_802154_irq_clear_pending(uint32_t irqn);
bool nrf_802154_irq_is_enabled(uint32_t irqn);
uint32_t nrf_802154_irq_priority_get(uint32_t irqn);

#endif /* TEST_NRF_802154_IRQ_H */
