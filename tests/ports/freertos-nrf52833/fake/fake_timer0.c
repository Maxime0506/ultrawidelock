#include <hal/nrf_timer.h>

#include <string.h>

fake_timer_t fake_timer0;
uint32_t fake_timer_us_per_capture = 50u;

void fake_timer_reset(void)
{
	memset(&fake_timer0, 0, sizeof(fake_timer0));
	fake_timer_us_per_capture = 50u;
}

void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task)
{
	if (task != NRF_TIMER_TASK_CAPTURE0) {
		return;
	}
	/*
	 * Charge for the work that happened since the last look. The part's
	 * timer runs on its own; the model advances it only when asked, which
	 * is enough to make a time-bounded loop end.
	 */
	p_reg->counter_us += fake_timer_us_per_capture;
	p_reg->cc[NRF_TIMER_CC_CHANNEL0] = p_reg->counter_us;
}

uint32_t nrf_timer_cc_get(const NRF_TIMER_Type *p_reg, nrf_timer_cc_channel_t channel)
{
	return p_reg->cc[channel];
}
