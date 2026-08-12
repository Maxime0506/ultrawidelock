/*
 * The POWER peripheral's reset-reason register, as far as the OpenThread misc
 * platform reads it. RESETREAS latches bits across resets and is cleared by
 * writing ones, which is the behaviour the model reproduces.
 */
#ifndef TEST_HAL_NRF_POWER_H
#define TEST_HAL_NRF_POWER_H

#include <stdint.h>

typedef struct {
	uint32_t resetreas;
	unsigned clear_calls;
} fake_power_t;

extern fake_power_t fake_power;
#define NRF_POWER (&fake_power)

typedef fake_power_t NRF_POWER_Type;

#define NRF_POWER_RESETREAS_RESETPIN_MASK (1uL << 0)
#define NRF_POWER_RESETREAS_DOG_MASK (1uL << 1)
#define NRF_POWER_RESETREAS_SREQ_MASK (1uL << 2)
#define NRF_POWER_RESETREAS_LOCKUP_MASK (1uL << 3)
#define NRF_POWER_RESETREAS_OFF_MASK (1uL << 16)

uint32_t nrf_power_resetreas_get(const NRF_POWER_Type *p_reg);
void nrf_power_resetreas_clear(NRF_POWER_Type *p_reg, uint32_t mask);

/* Test control. */
void fake_power_reset(void);

#endif /* TEST_HAL_NRF_POWER_H */
