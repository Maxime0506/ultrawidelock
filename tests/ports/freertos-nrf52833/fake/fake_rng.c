#include <hal/nrf_rng.h>

#include <nrfx.h>
#include <string.h>

/*
 * Whether the RNG vector was masked every time the model produced a byte. A
 * caller polling the peripheral must mask it, or the handler takes the byte the
 * caller is spinning for -- a race no host test can run into by accident, but
 * one the model can watch for.
 */
bool fake_rng_masked_on_every_produce = true;

fake_rng_t fake_rng;
unsigned fake_rng_auto_produce;

static uint8_t s_auto_value;

void fake_rng_reset(void)
{
	memset(&fake_rng, 0, sizeof(fake_rng));
	fake_rng_auto_produce = 0;
	s_auto_value = 0;
	fake_rng_masked_on_every_produce = true;
}

void nrf_rng_task_trigger(NRF_RNG_Type *p_reg, nrf_rng_task_t task)
{
	if (task == NRF_RNG_TASK_START) {
		p_reg->running = true;
		p_reg->starts++;
	} else {
		p_reg->running = false;
		p_reg->stops++;
	}
}

void nrf_rng_event_clear(NRF_RNG_Type *p_reg, nrf_rng_event_t event)
{
	if (event == NRF_RNG_EVENT_VALRDY) {
		p_reg->event_valrdy = false;
	}
}

bool nrf_rng_event_check(const NRF_RNG_Type *p_reg, nrf_rng_event_t event)
{
	if (event != NRF_RNG_EVENT_VALRDY) {
		return false;
	}
	if (!fake_rng.event_valrdy && fake_rng_auto_produce > 0u && fake_rng.running) {
		fake_rng_auto_produce--;
		s_auto_value++;
		fake_rng_produce(s_auto_value);
	}
	return p_reg->event_valrdy;
}

void nrf_rng_int_enable(NRF_RNG_Type *p_reg, uint32_t mask)
{
	if ((mask & NRF_RNG_INT_VALRDY_MASK) != 0u) {
		p_reg->int_valrdy = true;
	}
}

void nrf_rng_int_disable(NRF_RNG_Type *p_reg, uint32_t mask)
{
	if ((mask & NRF_RNG_INT_VALRDY_MASK) != 0u) {
		p_reg->int_valrdy = false;
	}
}

void nrf_rng_error_correction_enable(NRF_RNG_Type *p_reg)
{
	p_reg->error_correction = true;
}

uint8_t nrf_rng_random_value_get(const NRF_RNG_Type *p_reg)
{
	return p_reg->value;
}

void fake_rng_produce(uint8_t value)
{
	if (!fake_rng.running) {
		/*
		 * A stopped generator produces nothing. A driver that waits on
		 * VALRDY without starting the peripheral hangs on hardware, and
		 * this is what makes that visible here instead.
		 */
		fake_rng.violations++;
		return;
	}
	if (fake_nvic_get_enable_irq(RNG_IRQn) != 0u) {
		fake_rng_masked_on_every_produce = false;
	}
	fake_rng.value = value;
	fake_rng.event_valrdy = true;
}
