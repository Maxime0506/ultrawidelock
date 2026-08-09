#include <assert.h>
#include <string.h>

#include "fake_nrf.h"
#include <nrfx.h>

bool fake_nrf_irq_enabled[FAKE_NRF_IRQ_COUNT];
bool fake_nrf_irq_pending[FAKE_NRF_IRQ_COUNT];
uint32_t fake_nrf_irq_priority[FAKE_NRF_IRQ_COUNT];
unsigned fake_nrf_irq_disable_calls;
unsigned fake_nrf_irq_clear_calls;

static unsigned irq_index(IRQn_Type irq)
{
	assert(irq >= 0 && (uint32_t)irq < FAKE_NRF_IRQ_COUNT);
	return (unsigned)irq;
}

void fake_nrf_reset(void)
{
	memset(fake_nrf_irq_enabled, 0, sizeof(fake_nrf_irq_enabled));
	memset(fake_nrf_irq_pending, 0, sizeof(fake_nrf_irq_pending));
	memset(fake_nrf_irq_priority, 0, sizeof(fake_nrf_irq_priority));
	fake_nrf_irq_disable_calls = 0;
	fake_nrf_irq_clear_calls = 0;
}

void fake_nvic_disable_irq(IRQn_Type irq)
{
	fake_nrf_irq_enabled[irq_index(irq)] = false;
	fake_nrf_irq_disable_calls++;
}

void fake_nvic_enable_irq(IRQn_Type irq)
{
	fake_nrf_irq_enabled[irq_index(irq)] = true;
}

void fake_nvic_set_pending_irq(IRQn_Type irq)
{
	fake_nrf_irq_pending[irq_index(irq)] = true;
}

void fake_nvic_clear_pending_irq(IRQn_Type irq)
{
	fake_nrf_irq_pending[irq_index(irq)] = false;
	fake_nrf_irq_clear_calls++;
}

uint32_t fake_nvic_get_enable_irq(IRQn_Type irq)
{
	return fake_nrf_irq_enabled[irq_index(irq)] ? 1u : 0u;
}

void fake_nvic_set_priority(IRQn_Type irq, uint32_t priority)
{
	fake_nrf_irq_priority[irq_index(irq)] = priority;
}

uint32_t fake_nvic_get_priority(IRQn_Type irq)
{
	return fake_nrf_irq_priority[irq_index(irq)];
}
