#include <hal/nrf_power.h>

#include <string.h>

fake_power_t fake_power;

void fake_power_reset(void)
{
	memset(&fake_power, 0, sizeof(fake_power));
}

uint32_t nrf_power_resetreas_get(const NRF_POWER_Type *p_reg)
{
	return p_reg->resetreas;
}

/* A field is cleared by writing a one to it, as on the part. */
void nrf_power_resetreas_clear(NRF_POWER_Type *p_reg, uint32_t mask)
{
	p_reg->resetreas &= ~mask;
	p_reg->clear_calls++;
}
