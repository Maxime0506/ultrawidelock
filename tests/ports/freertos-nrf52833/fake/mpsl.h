/* Subset of the pinned nrfxlib mpsl.h. */
#ifndef TEST_MPSL_H
#define TEST_MPSL_H

#include <stdint.h>

#include <mpsl_clock.h>
#include <nrfx.h>

typedef void (*mpsl_assert_handler_t)(const char *file, const uint32_t line);

int32_t mpsl_init(mpsl_clock_lfclk_cfg_t const *p_clock_config, IRQn_Type low_prio_irq,
		  mpsl_assert_handler_t p_assert_handler);
void mpsl_uninit(void);
void mpsl_low_priority_process(void);

void MPSL_IRQ_RADIO_Handler(void);
void MPSL_IRQ_RTC0_Handler(void);
void MPSL_IRQ_TIMER0_Handler(void);
void MPSL_IRQ_CLOCK_Handler(void);

#endif /* TEST_MPSL_H */
