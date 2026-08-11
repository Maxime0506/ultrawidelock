/*
 * Just enough of the nRF52 TIMER to model MPSL's TIMER0 inside a timeslot.
 *
 * MPSL resets TIMER0 at the start of every timeslot and documents it as
 * readable there, which is how the flash work measures its remaining budget
 * without assuming a programming time. The model charges a fixed cost per
 * capture, so a bounded loop terminates here for the same reason it terminates
 * on the part: the clock runs out.
 */
#ifndef TEST_HAL_NRF_TIMER_H
#define TEST_HAL_NRF_TIMER_H

#include <stdint.h>

typedef struct {
	uint32_t counter_us;
	uint32_t cc[4];
} fake_timer_t;

extern fake_timer_t fake_timer0;
#define NRF_TIMER0 (&fake_timer0)

typedef fake_timer_t NRF_TIMER_Type;

typedef enum {
	NRF_TIMER_TASK_CAPTURE0 = 0x140,
} nrf_timer_task_t;

typedef enum {
	NRF_TIMER_CC_CHANNEL0 = 0,
} nrf_timer_cc_channel_t;

void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task);
uint32_t nrf_timer_cc_get(const NRF_TIMER_Type *p_reg, nrf_timer_cc_channel_t channel);

/* Test control. */
void fake_timer_reset(void);
/* Microseconds the model charges for each capture. */
extern uint32_t fake_timer_us_per_capture;

#endif /* TEST_HAL_NRF_TIMER_H */
