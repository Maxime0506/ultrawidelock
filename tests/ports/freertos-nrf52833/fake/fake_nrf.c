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

static unsigned s_system_resets;

unsigned fake_system_reset_count(void)
{
	return s_system_resets;
}

void fake_system_reset(void)
{
	s_system_resets++;
}

static uint32_t s_ipsr;
static uint32_t s_primask;
static unsigned s_primask_disable_calls;

uint32_t fake_ipsr_get(void)
{
	return s_ipsr;
}

void fake_ipsr_set(uint32_t value)
{
	s_ipsr = value;
}

uint32_t fake_primask_get(void)
{
	return s_primask;
}

void fake_primask_set(uint32_t value)
{
	s_primask = value;
}

void fake_primask_disable_irq(void)
{
	s_primask = 1u;
	s_primask_disable_calls++;
}

unsigned fake_primask_disable_count(void)
{
	return s_primask_disable_calls;
}

void fake_primask_reset(void)
{
	s_system_resets = 0;
	s_ipsr = 0;
	s_primask = 0;
	s_primask_disable_calls = 0;
}
