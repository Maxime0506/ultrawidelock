/*
 * Register-level model of the nRF52 PPI, limited to the endpoint calls the
 * low-power timer platform makes. Channel ownership belongs to the 802.15.4
 * driver core, which allocates the channel and sets its task endpoint, so only
 * the event endpoint is modelled here.
 */
#ifndef TEST_HAL_NRF_PPI_H
#define TEST_HAL_NRF_PPI_H

#include <stdint.h>

#define NRF_PPI_CHANNEL_COUNT 20u

typedef struct {
	uint32_t event_endpoint[NRF_PPI_CHANNEL_COUNT];
	uint32_t task_endpoint[NRF_PPI_CHANNEL_COUNT];
} fake_ppi_t;

extern fake_ppi_t fake_ppi;
#define NRF_PPI (&fake_ppi)

typedef fake_ppi_t NRF_PPI_Type;

typedef enum {
	NRF_PPI_CHANNEL0 = 0,
} nrf_ppi_channel_t;

void nrf_ppi_event_endpoint_setup(NRF_PPI_Type *p_reg, nrf_ppi_channel_t channel, uint32_t eep);
uint32_t nrf_ppi_event_endpoint_get(const NRF_PPI_Type *p_reg, nrf_ppi_channel_t channel);

/* Test control. */
void fake_ppi_reset(void);

#endif /* TEST_HAL_NRF_PPI_H */
