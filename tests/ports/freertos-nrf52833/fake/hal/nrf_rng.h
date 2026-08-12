/*
 * Register-level model of the nRF52 RNG, matching the pinned hal_nordic
 * hal/nrf_rng.h surface the port uses.
 *
 * The generator does not produce on its own: the test calls fake_rng_produce()
 * to stand for the noise source finishing a byte, and the model refuses to
 * produce while the generator is stopped. That is the rule worth enforcing --
 * a driver that reads VALUE without starting the peripheral waits forever on
 * hardware and would pass against a model that just hands over a number.
 */
#ifndef TEST_HAL_NRF_RNG_H
#define TEST_HAL_NRF_RNG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool running;
	bool error_correction;
	bool int_valrdy;
	bool event_valrdy;
	uint8_t value;
	unsigned starts;
	unsigned stops;
	/* Bytes the model refused to produce because the generator was stopped. */
	unsigned violations;
} fake_rng_t;

extern fake_rng_t fake_rng;
#define NRF_RNG (&fake_rng)

typedef fake_rng_t NRF_RNG_Type;

typedef enum {
	NRF_RNG_TASK_START = 0x00,
	NRF_RNG_TASK_STOP = 0x04,
} nrf_rng_task_t;

typedef enum {
	NRF_RNG_EVENT_VALRDY = 0x100,
} nrf_rng_event_t;

#define NRF_RNG_INT_VALRDY_MASK (1uL << 0)

void nrf_rng_task_trigger(NRF_RNG_Type *p_reg, nrf_rng_task_t task);
void nrf_rng_event_clear(NRF_RNG_Type *p_reg, nrf_rng_event_t event);
bool nrf_rng_event_check(const NRF_RNG_Type *p_reg, nrf_rng_event_t event);
void nrf_rng_int_enable(NRF_RNG_Type *p_reg, uint32_t mask);
void nrf_rng_int_disable(NRF_RNG_Type *p_reg, uint32_t mask);
void nrf_rng_error_correction_enable(NRF_RNG_Type *p_reg);
uint8_t nrf_rng_random_value_get(const NRF_RNG_Type *p_reg);

/* Test control. */
void fake_rng_reset(void);
/*
 * Stand for the noise source finishing one byte: raise VALRDY and load VALUE.
 * Refused, and counted as a violation, while the generator is stopped.
 */
void fake_rng_produce(uint8_t value);
/*
 * Produce a byte from inside the next VALRDY reads, so a caller that polls the
 * peripheral directly terminates the way it would on hardware. Zero disables
 * it, which is a generator that never answers.
 */
extern unsigned fake_rng_auto_produce;
/* False once the model produced a byte with the RNG vector still unmasked. */
extern bool fake_rng_masked_on_every_produce;

#endif /* TEST_HAL_NRF_RNG_H */
