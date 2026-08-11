/*
 * Register-level model of the nRF52 RTC, matching the pinned hal_nordic
 * hal/nrf_rtc.h surface the port uses. The counter does not run on its own:
 * the test advances it with fake_rtc_advance(), which raises the compare and
 * overflow events exactly as the hardware would.
 */
#ifndef TEST_HAL_NRF_RTC_H
#define TEST_HAL_NRF_RTC_H

#include <stdbool.h>
#include <stdint.h>

#define NRF_RTC_COUNTER_BITS 24u
#define NRF_RTC_COUNTER_SPAN (1uL << NRF_RTC_COUNTER_BITS)
#define NRF_RTC_COUNTER_MAX (NRF_RTC_COUNTER_SPAN - 1uL)
#define NRF_RTC_CHANNEL_COUNT 4u

typedef struct {
	uint32_t counter;
	uint32_t prescaler;
	uint32_t cc[NRF_RTC_CHANNEL_COUNT];
	bool event_compare[NRF_RTC_CHANNEL_COUNT];
	bool event_overflow;
	uint32_t int_mask;
	/*
	 * EVTEN. The RTC only raises an event when its bit is set here, which is
	 * why the model refuses to raise one otherwise: a driver that enables the
	 * interrupt and forgets the event would wait forever on hardware.
	 */
	uint32_t evt_mask;
	bool running;
	unsigned start_calls;
	unsigned stop_calls;
	unsigned clear_calls;
} fake_rtc_t;

extern fake_rtc_t fake_rtc2;
#define NRF_RTC2 (&fake_rtc2)

typedef fake_rtc_t NRF_RTC_Type;

typedef enum {
	NRF_RTC_EVENT_OVERFLOW = 0x100,
	NRF_RTC_EVENT_COMPARE_0 = 0x140,
	NRF_RTC_EVENT_COMPARE_1 = 0x144,
	NRF_RTC_EVENT_COMPARE_2 = 0x148,
	NRF_RTC_EVENT_COMPARE_3 = 0x14c,
} nrf_rtc_event_t;

typedef enum {
	NRF_RTC_TASK_START = 0x00,
	NRF_RTC_TASK_STOP = 0x04,
	NRF_RTC_TASK_CLEAR = 0x08,
} nrf_rtc_task_t;

#define NRF_RTC_INT_OVERFLOW_MASK (1uL << 1)
#define NRF_RTC_INT_COMPARE0_MASK (1uL << 16)
#define NRF_RTC_INT_COMPARE1_MASK (1uL << 17)
#define NRF_RTC_INT_COMPARE2_MASK (1uL << 18)
#define NRF_RTC_INT_COMPARE3_MASK (1uL << 19)
/* EVTEN uses the same bit positions as INTEN. */
#define NRF_RTC_CHANNEL_EVT_MASK(ch) (1uL << (16u + (ch)))

nrf_rtc_event_t nrf_rtc_compare_event_get(uint8_t index);
void nrf_rtc_cc_set(NRF_RTC_Type *p_reg, uint32_t ch, uint32_t cc_val);
uint32_t nrf_rtc_cc_get(const NRF_RTC_Type *p_reg, uint32_t ch);
void nrf_rtc_int_enable(NRF_RTC_Type *p_reg, uint32_t mask);
void nrf_rtc_int_disable(NRF_RTC_Type *p_reg, uint32_t mask);
uint32_t nrf_rtc_int_enable_check(const NRF_RTC_Type *p_reg, uint32_t mask);
void nrf_rtc_event_enable(NRF_RTC_Type *p_reg, uint32_t mask);
void nrf_rtc_event_disable(NRF_RTC_Type *p_reg, uint32_t mask);
bool nrf_rtc_event_check(const NRF_RTC_Type *p_reg, nrf_rtc_event_t event);
void nrf_rtc_event_clear(NRF_RTC_Type *p_reg, nrf_rtc_event_t event);
uint32_t nrf_rtc_counter_get(const NRF_RTC_Type *p_reg);
void nrf_rtc_prescaler_set(NRF_RTC_Type *p_reg, uint32_t val);
uint32_t nrf_rtc_event_address_get(const NRF_RTC_Type *p_reg, nrf_rtc_event_t event);
void nrf_rtc_task_trigger(NRF_RTC_Type *p_reg, nrf_rtc_task_t task);

/* Test control. */
void fake_rtc_reset(void);
/* Advance the counter, raising compare and overflow events on the way. */
void fake_rtc_advance(uint32_t ticks);
/*
 * Advance the counter once, from inside the next compare write. This is the
 * only way to reproduce a deadline that goes past while it is being programmed,
 * which is the race both the software timer and the hardware task re-check for.
 */
void fake_rtc_advance_on_next_cc_set(uint32_t ticks);

#endif /* TEST_HAL_NRF_RTC_H */
