#include <hal/nrf_rtc.h>

#include <string.h>

fake_rtc_t fake_rtc2;

void fake_rtc_reset(void)
{
	memset(&fake_rtc2, 0, sizeof(fake_rtc2));
}

nrf_rtc_event_t nrf_rtc_compare_event_get(uint8_t index)
{
	return (nrf_rtc_event_t)(NRF_RTC_EVENT_COMPARE_0 + (uint32_t)index * 4u);
}

static uint32_t event_channel(nrf_rtc_event_t event)
{
	return ((uint32_t)event - (uint32_t)NRF_RTC_EVENT_COMPARE_0) / 4u;
}

void nrf_rtc_cc_set(NRF_RTC_Type *p_reg, uint32_t ch, uint32_t cc_val)
{
	p_reg->cc[ch] = cc_val & NRF_RTC_COUNTER_MAX;
}

uint32_t nrf_rtc_cc_get(const NRF_RTC_Type *p_reg, uint32_t ch)
{
	return p_reg->cc[ch];
}

void nrf_rtc_int_enable(NRF_RTC_Type *p_reg, uint32_t mask)
{
	p_reg->int_mask |= mask;
}

void nrf_rtc_int_disable(NRF_RTC_Type *p_reg, uint32_t mask)
{
	p_reg->int_mask &= ~mask;
}

uint32_t nrf_rtc_int_enable_check(const NRF_RTC_Type *p_reg, uint32_t mask)
{
	return p_reg->int_mask & mask;
}

bool nrf_rtc_event_check(const NRF_RTC_Type *p_reg, nrf_rtc_event_t event)
{
	if (event == NRF_RTC_EVENT_OVERFLOW) {
		return p_reg->event_overflow;
	}
	return p_reg->event_compare[event_channel(event)];
}

void nrf_rtc_event_clear(NRF_RTC_Type *p_reg, nrf_rtc_event_t event)
{
	if (event == NRF_RTC_EVENT_OVERFLOW) {
		p_reg->event_overflow = false;
		return;
	}
	p_reg->event_compare[event_channel(event)] = false;
}

uint32_t nrf_rtc_counter_get(const NRF_RTC_Type *p_reg)
{
	return p_reg->counter;
}

void nrf_rtc_prescaler_set(NRF_RTC_Type *p_reg, uint32_t val)
{
	p_reg->prescaler = val;
}

uint32_t nrf_rtc_event_address_get(const NRF_RTC_Type *p_reg, nrf_rtc_event_t event)
{
	return (uint32_t)(uintptr_t)p_reg + (uint32_t)event;
}

void nrf_rtc_task_trigger(NRF_RTC_Type *p_reg, nrf_rtc_task_t task)
{
	switch (task) {
	case NRF_RTC_TASK_START:
		p_reg->running = true;
		p_reg->start_calls++;
		break;
	case NRF_RTC_TASK_STOP:
		p_reg->running = false;
		p_reg->stop_calls++;
		break;
	case NRF_RTC_TASK_CLEAR:
		p_reg->counter = 0;
		p_reg->clear_calls++;
		break;
	}
}

/*
 * Steps the counter one tick at a time so a compare is only raised on an exact
 * match, which is what the hardware does and what the driver's minimum compare
 * distance exists to cope with.
 */
void fake_rtc_advance(uint32_t ticks)
{
	uint32_t i;
	uint32_t ch;

	if (!fake_rtc2.running) {
		return;
	}

	for (i = 0; i < ticks; i++) {
		fake_rtc2.counter = (fake_rtc2.counter + 1u) & NRF_RTC_COUNTER_MAX;
		if (fake_rtc2.counter == 0u) {
			fake_rtc2.event_overflow = true;
		}
		for (ch = 0; ch < NRF_RTC_CHANNEL_COUNT; ch++) {
			if (fake_rtc2.counter == fake_rtc2.cc[ch]) {
				fake_rtc2.event_compare[ch] = true;
			}
		}
	}
}
