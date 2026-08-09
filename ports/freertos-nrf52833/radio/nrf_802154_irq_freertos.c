#include <platform/nrf_802154_irq.h>

#include "FreeRTOS.h"
#include <nrfx.h>

void nrf_802154_irq_init(uint32_t irqn, int32_t prio, nrf_802154_isr_t isr)
{
	IRQn_Type irq = (IRQn_Type)irqn;

	/* The Qorvo vector table provides weak stubs, and the nRF 802.15.4 driver
	 * links its internal named handlers over them. No writable vector table is
	 * required, so the callback is evidence for the linked handler only. */
	configASSERT(isr != NULL);
	NVIC_DisableIRQ(irq);
	NVIC_ClearPendingIRQ(irq);
	NVIC_SetPriority(irq, (uint32_t)(prio < 0 ? 0 : prio));
}

void nrf_802154_irq_enable(uint32_t irqn)
{
	NVIC_EnableIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_disable(uint32_t irqn)
{
	NVIC_DisableIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_set_pending(uint32_t irqn)
{
	NVIC_SetPendingIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_clear_pending(uint32_t irqn)
{
	NVIC_ClearPendingIRQ((IRQn_Type)irqn);
}

bool nrf_802154_irq_is_enabled(uint32_t irqn)
{
	return NVIC_GetEnableIRQ((IRQn_Type)irqn) != 0u;
}

uint32_t nrf_802154_irq_priority_get(uint32_t irqn)
{
	return NVIC_GetPriority((IRQn_Type)irqn);
}
