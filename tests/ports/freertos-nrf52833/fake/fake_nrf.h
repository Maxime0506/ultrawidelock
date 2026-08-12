#ifndef TEST_FAKE_NRF_H
#define TEST_FAKE_NRF_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_NRF_IRQ_COUNT 64u

extern bool fake_nrf_irq_enabled[FAKE_NRF_IRQ_COUNT];
extern bool fake_nrf_irq_pending[FAKE_NRF_IRQ_COUNT];
extern uint32_t fake_nrf_irq_priority[FAKE_NRF_IRQ_COUNT];
extern unsigned fake_nrf_irq_disable_calls;
extern unsigned fake_nrf_irq_clear_calls;

void fake_nrf_reset(void);

#endif /* TEST_FAKE_NRF_H */
