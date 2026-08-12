/* Register-level nRF52833 GPIOTE model. See hal/nrf_gpiote.h for the rules. */
#include "hal/nrf_gpiote.h"

#include <string.h>

#include "hal/nrf_gpio.h"

fake_gpiote_channel_t fake_gpiote[FAKE_GPIOTE_CHANNELS];
uint32_t fake_gpiote_int_mask;
unsigned fake_gpiote_missed_clears;
unsigned fake_gpiote_unwatched_edges;

void fake_gpiote_reset(void)
{
	memset(fake_gpiote, 0, sizeof(fake_gpiote));
	fake_gpiote_int_mask = 0;
	fake_gpiote_missed_clears = 0;
	fake_gpiote_unwatched_edges = 0;
}

static uint32_t channel_of(nrf_gpiote_events_t event)
{
	return ((uint32_t)event - (uint32_t)NRF_GPIOTE_EVENTS_IN_0) / 4u;
}

void nrf_gpiote_event_configure(uint32_t idx, uint32_t pin, nrf_gpiote_polarity_t polarity)
{
	if (idx >= FAKE_GPIOTE_CHANNELS) {
		return;
	}
	fake_gpiote[idx].configured = true;
	fake_gpiote[idx].pin = pin;
	fake_gpiote[idx].polarity = polarity;
}

void nrf_gpiote_event_enable(uint32_t idx)
{
	if (idx < FAKE_GPIOTE_CHANNELS) {
		fake_gpiote[idx].enabled = true;
	}
}

void nrf_gpiote_event_disable(uint32_t idx)
{
	if (idx < FAKE_GPIOTE_CHANNELS) {
		fake_gpiote[idx].enabled = false;
	}
}

bool nrf_gpiote_event_is_set(nrf_gpiote_events_t event)
{
	uint32_t idx = channel_of(event);

	return idx < FAKE_GPIOTE_CHANNELS && fake_gpiote[idx].event;
}

void nrf_gpiote_event_clear(nrf_gpiote_events_t event)
{
	uint32_t idx = channel_of(event);

	if (idx < FAKE_GPIOTE_CHANNELS) {
		fake_gpiote[idx].event = false;
	}
}

void nrf_gpiote_int_enable(uint32_t mask)
{
	fake_gpiote_int_mask |= mask;
}

void nrf_gpiote_int_disable(uint32_t mask)
{
	fake_gpiote_int_mask &= ~mask;
}

uint32_t nrf_gpiote_int_is_enabled(uint32_t mask)
{
	return fake_gpiote_int_mask & mask;
}

bool fake_gpiote_pin_edge(uint32_t pin, bool level)
{
	bool was = nrf_gpio_pin_read(pin) != 0u;
	bool raised = false;
	uint32_t i;

	fake_gpio_input_set(pin, level);
	if (was == level) {
		return false;
	}

	for (i = 0; i < FAKE_GPIOTE_CHANNELS; i++) {
		bool matches;

		if (!fake_gpiote[i].configured || !fake_gpiote[i].enabled ||
		    fake_gpiote[i].pin != pin) {
			continue;
		}
		matches = fake_gpiote[i].polarity == NRF_GPIOTE_POLARITY_TOGGLE ||
			  (fake_gpiote[i].polarity == NRF_GPIOTE_POLARITY_LOTOHI && level) ||
			  (fake_gpiote[i].polarity == NRF_GPIOTE_POLARITY_HITOLO && !level);
		if (!matches) {
			continue;
		}
		if (fake_gpiote[i].event) {
			/* Latched and never cleared: on the part this re-enters
			 * the handler forever at its own priority. */
			fake_gpiote_missed_clears++;
		}
		fake_gpiote[i].event = true;
		raised = true;
	}
	if (!raised) {
		fake_gpiote_unwatched_edges++;
	}
	return raised;
}

bool fake_gpiote_would_interrupt(void)
{
	uint32_t i;

	for (i = 0; i < FAKE_GPIOTE_CHANNELS; i++) {
		if (fake_gpiote[i].event && (fake_gpiote_int_mask & (1uL << i)) != 0u) {
			return true;
		}
	}
	return false;
}
