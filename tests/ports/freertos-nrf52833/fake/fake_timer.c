#include <hal/nrf_timer.h>

#include <string.h>

fake_timer_t fake_timer1;

void fake_timer_reset(void)
{
	memset(&fake_timer1, 0, sizeof(fake_timer1));
}

static uint32_t capture_channel(nrf_timer_task_t task)
{
	return ((uint32_t)task - (uint32_t)NRF_TIMER_TASK_CAPTURE0) / 4u;
}

void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task)
{
	switch (task) {
	case NRF_TIMER_TASK_START:
		p_reg->running = true;
		p_reg->start_calls++;
		break;
	case NRF_TIMER_TASK_STOP:
		p_reg->running = false;
		break;
	case NRF_TIMER_TASK_CLEAR:
		p_reg->counter = 0;
		break;
	case NRF_TIMER_TASK_SHUTDOWN:
		p_reg->running = false;
		p_reg->shutdown_calls++;
		break;
	case NRF_TIMER_TASK_CAPTURE0:
	case NRF_TIMER_TASK_CAPTURE1:
	case NRF_TIMER_TASK_CAPTURE2:
	case NRF_TIMER_TASK_CAPTURE3:
		p_reg->cc[capture_channel(task)] = p_reg->counter;
		break;
	}
}

uint32_t nrf_timer_task_address_get(const NRF_TIMER_Type *p_reg, nrf_timer_task_t task)
{
	return (uint32_t)(uintptr_t)p_reg + (uint32_t)task;
}

uint32_t nrf_timer_cc_get(const NRF_TIMER_Type *p_reg, uint32_t ch)
{
	return p_reg->cc[ch];
}

void nrf_timer_cc_set(NRF_TIMER_Type *p_reg, uint32_t ch, uint32_t cc_val)
{
	p_reg->cc[ch] = cc_val;
}

void nrf_timer_bit_width_set(NRF_TIMER_Type *p_reg, nrf_timer_bit_width_t width)
{
	p_reg->bit_width = (uint32_t)width;
}

void nrf_timer_prescaler_set(NRF_TIMER_Type *p_reg, uint32_t prescaler)
{
	p_reg->prescaler = prescaler;
}

void nrf_timer_mode_set(NRF_TIMER_Type *p_reg, nrf_timer_mode_t mode)
{
	p_reg->mode = (uint32_t)mode;
}

void fake_timer_advance(uint32_t ticks)
{
	if (fake_timer1.running) {
		fake_timer1.counter += ticks;
	}
}

void fake_timer_capture(uint32_t ch)
{
	fake_timer1.cc[ch] = fake_timer1.counter;
}
