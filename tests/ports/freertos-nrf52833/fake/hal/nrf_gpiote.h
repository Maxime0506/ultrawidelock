/*
 * Register-level model of the nRF52833 GPIOTE, matching the pinned
 * hal/nrf_gpiote.h surface the port uses.
 *
 * The model does not raise an event unless the channel would: it has to be
 * configured for the pin, enabled, and the transition has to match the
 * configured polarity. A driver that enables the interrupt but never binds the
 * channel to a pin gets no event on hardware, and its ranging simply never
 * starts -- against a permissive fake that same driver looks fine.
 *
 * The event does not clear itself either. GPIOTE latches, so a handler that
 * returns without clearing re-enters immediately and the part livelocks at the
 * handler's priority. The model counts every time an event is raised while one
 * is already pending, which is what that bug looks like from outside.
 */
#ifndef TEST_HAL_NRF_GPIOTE_H
#define TEST_HAL_NRF_GPIOTE_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_GPIOTE_CHANNELS 8u

typedef enum {
	NRF_GPIOTE_POLARITY_LOTOHI = 1,
	NRF_GPIOTE_POLARITY_HITOLO = 2,
	NRF_GPIOTE_POLARITY_TOGGLE = 3,
} nrf_gpiote_polarity_t;

typedef enum {
	NRF_GPIOTE_EVENTS_IN_0 = 0x100,
	NRF_GPIOTE_EVENTS_IN_1 = 0x104,
	NRF_GPIOTE_EVENTS_IN_2 = 0x108,
	NRF_GPIOTE_EVENTS_IN_3 = 0x10C,
	NRF_GPIOTE_EVENTS_IN_4 = 0x110,
	NRF_GPIOTE_EVENTS_IN_5 = 0x114,
	NRF_GPIOTE_EVENTS_IN_6 = 0x118,
	NRF_GPIOTE_EVENTS_IN_7 = 0x11C,
} nrf_gpiote_events_t;

#define NRF_GPIOTE_INT_IN0_MASK (1uL << 0)
#define NRF_GPIOTE_INT_IN1_MASK (1uL << 1)

typedef struct {
	bool configured;
	bool enabled;
	uint32_t pin;
	nrf_gpiote_polarity_t polarity;
	bool event;
} fake_gpiote_channel_t;

extern fake_gpiote_channel_t fake_gpiote[FAKE_GPIOTE_CHANNELS];
extern uint32_t fake_gpiote_int_mask;
/* Events raised onto an event that was already pending: a missed clear. */
extern unsigned fake_gpiote_missed_clears;
/* Transitions the model dropped because no channel was watching for them. */
extern unsigned fake_gpiote_unwatched_edges;

void nrf_gpiote_event_configure(uint32_t idx, uint32_t pin, nrf_gpiote_polarity_t polarity);
void nrf_gpiote_event_enable(uint32_t idx);
void nrf_gpiote_event_disable(uint32_t idx);
bool nrf_gpiote_event_is_set(nrf_gpiote_events_t event);
void nrf_gpiote_event_clear(nrf_gpiote_events_t event);
void nrf_gpiote_int_enable(uint32_t mask);
void nrf_gpiote_int_disable(uint32_t mask);
uint32_t nrf_gpiote_int_is_enabled(uint32_t mask);

void fake_gpiote_reset(void);

/*
 * Move a pin and let GPIOTE see it. Returns true if a channel raised an event,
 * which is the model's answer to "would the chip have interrupted here".
 */
bool fake_gpiote_pin_edge(uint32_t pin, bool level);

/* Whether the vector would run now: an event pending and its interrupt enabled. */
bool fake_gpiote_would_interrupt(void);

#endif /* TEST_HAL_NRF_GPIOTE_H */
