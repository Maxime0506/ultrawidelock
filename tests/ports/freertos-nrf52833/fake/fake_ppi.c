#include <hal/nrf_ppi.h>

#include <string.h>

fake_ppi_t fake_ppi;

void fake_ppi_reset(void)
{
	memset(&fake_ppi, 0, sizeof(fake_ppi));
}

void nrf_ppi_event_endpoint_setup(NRF_PPI_Type *p_reg, nrf_ppi_channel_t channel, uint32_t eep)
{
	p_reg->event_endpoint[channel] = eep;
}

uint32_t nrf_ppi_event_endpoint_get(const NRF_PPI_Type *p_reg, nrf_ppi_channel_t channel)
{
	return p_reg->event_endpoint[channel];
}
