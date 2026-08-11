/*
 * The factory information registers, as far as the pinned radio platform reads
 * them: two device-identifier words, which it turns into the EUI-64. The model
 * returns a fixed pair so the derived address is checkable.
 */
#ifndef TEST_HAL_NRF_FICR_H
#define TEST_HAL_NRF_FICR_H

#include <stdint.h>

typedef struct {
	uint32_t deviceid[2];
} NRF_FICR_Type;

extern NRF_FICR_Type fake_ficr;
#define NRF_FICR (&fake_ficr)

uint32_t nrf_ficr_deviceid_get(const NRF_FICR_Type *p_reg, uint32_t index);

#endif /* TEST_HAL_NRF_FICR_H */
