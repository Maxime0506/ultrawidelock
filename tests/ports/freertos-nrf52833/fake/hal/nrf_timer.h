/*
 * Register-level model of the nRF52 TIMER, matching the pinned hal_nordic
 * hal/nrf_timer.h surface that the vendor high-precision timer platform uses.
 * The counter does not free-run: fake_timer_advance() moves it, and a capture
 * task latches it into the channel's CC exactly as the hardware would.
 *
 * Two instances, because two things in this port use a TIMER and they use it
 * differently. TIMER1 is the vendor high-precision timer platform, driven by
 * fake_timer_advance(). TIMER0 is MPSL's, and board/flash_freertos.c reads it
 * inside a timeslot to measure how much of the slot is left; MPSL resets it at
 * every slot start and documents it as readable there. That one charges a fixed
 * cost per capture, so a bounded loop terminates here for the same reason it
 * terminates on the part: the clock runs out.
 *
 * They were briefly separate headers, and the second overwrote the first, which
 * left the high-precision timer gate unable to compile. One model of one
 * peripheral is the arrangement that cannot drift apart again.
 */
#ifndef TEST_HAL_NRF_TIMER_H
#define TEST_HAL_NRF_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define NRF_TIMER_CC_CHANNEL_COUNT 6u

typedef struct {
	uint32_t counter;
	/*
	 * TIMER0's microsecond count, kept separate from the tick counter
	 * because the two instances are read in different units: the
	 * high-precision timer platform works in ticks it advances itself,
	 * while the flash driver reads microseconds MPSL zeroes at slot start.
	 */
	uint32_t counter_us;
	uint32_t cc[NRF_TIMER_CC_CHANNEL_COUNT];
	uint32_t prescaler;
	uint32_t bit_width;
	uint32_t mode;
	bool running;
	unsigned start_calls;
	unsigned shutdown_calls;
} fake_timer_t;

extern fake_timer_t fake_timer1;
#define NRF_TIMER1 (&fake_timer1)

extern fake_timer_t fake_timer0;
#define NRF_TIMER0 (&fake_timer0)

typedef fake_timer_t NRF_TIMER_Type;

typedef enum {
	NRF_TIMER_CC_CHANNEL0 = 0,
	NRF_TIMER_CC_CHANNEL1 = 1,
	NRF_TIMER_CC_CHANNEL2 = 2,
	NRF_TIMER_CC_CHANNEL3 = 3,
} nrf_timer_cc_channel_t;

typedef enum {
	NRF_TIMER_TASK_START = 0x00,
	NRF_TIMER_TASK_STOP = 0x04,
	NRF_TIMER_TASK_CLEAR = 0x0c,
	NRF_TIMER_TASK_SHUTDOWN = 0x10,
	NRF_TIMER_TASK_CAPTURE0 = 0x40,
	NRF_TIMER_TASK_CAPTURE1 = 0x44,
	NRF_TIMER_TASK_CAPTURE2 = 0x48,
	NRF_TIMER_TASK_CAPTURE3 = 0x4c,
} nrf_timer_task_t;

typedef enum {
	NRF_TIMER_EVENT_COMPARE0 = 0x140,
	NRF_TIMER_EVENT_COMPARE1 = 0x144,
	NRF_TIMER_EVENT_COMPARE2 = 0x148,
	NRF_TIMER_EVENT_COMPARE3 = 0x14c,
} nrf_timer_event_t;

typedef enum {
	NRF_TIMER_MODE_TIMER = 0,
	NRF_TIMER_MODE_COUNTER = 1,
} nrf_timer_mode_t;

typedef enum {
	NRF_TIMER_BIT_WIDTH_16 = 0,
	NRF_TIMER_BIT_WIDTH_32 = 3,
} nrf_timer_bit_width_t;

/* The pinned HAL spells 1 MHz as prescaler 4 on a 16 MHz peripheral clock. */
typedef enum {
	NRF_TIMER_FREQ_16MHz = 0,
	NRF_TIMER_FREQ_1MHz = 4,
} nrf_timer_frequency_t;

#define NRF_TIMER_INT_COMPARE2_MASK (1uL << 18)
#define NRF_TIMER_INT_COMPARE3_MASK (1uL << 19)

void nrf_timer_task_trigger(NRF_TIMER_Type *p_reg, nrf_timer_task_t task);
uint32_t nrf_timer_task_address_get(const NRF_TIMER_Type *p_reg, nrf_timer_task_t task);
uint32_t nrf_timer_cc_get(const NRF_TIMER_Type *p_reg, uint32_t ch);
void nrf_timer_cc_set(NRF_TIMER_Type *p_reg, uint32_t ch, uint32_t cc_val);
void nrf_timer_bit_width_set(NRF_TIMER_Type *p_reg, nrf_timer_bit_width_t width);
void nrf_timer_prescaler_set(NRF_TIMER_Type *p_reg, uint32_t prescaler);
void nrf_timer_mode_set(NRF_TIMER_Type *p_reg, nrf_timer_mode_t mode);

/* Test control. */
void fake_timer_reset(void);
void fake_timer_advance(uint32_t ticks);
/*
 * Microseconds TIMER0 charges for each capture, which is how the flash test
 * makes a timeslot run out. Zero leaves TIMER0 still, which is the case where
 * the radio is down and the whole operation runs in one pass.
 */
extern uint32_t fake_timer_us_per_capture;
/* Latch the counter into a channel, as a PPI-driven capture task would. */
void fake_timer_capture(uint32_t ch);

#endif /* TEST_HAL_NRF_TIMER_H */
