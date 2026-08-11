#ifndef TEST_FAKE_SDC_H
#define TEST_FAKE_SDC_H

#include <stdbool.h>
#include <stdint.h>

#include <mpsl.h>
#include <sdc.h>
#include <sdc_soc.h>

#define FAKE_SDC_MAX_CFG 8

struct fake_sdc_cfg_record {
	uint8_t tag;
	uint8_t type;
	sdc_cfg_t cfg;
};

/* MPSL */
extern unsigned fake_mpsl_init_calls;
extern mpsl_clock_lfclk_cfg_t fake_mpsl_clock_cfg;
extern IRQn_Type fake_mpsl_low_prio_irq;
extern mpsl_assert_handler_t fake_mpsl_assert_handler;
extern int32_t fake_mpsl_init_result;
extern unsigned fake_mpsl_low_priority_process_calls;
extern unsigned fake_mpsl_hfclk_request_calls;
extern unsigned fake_mpsl_hfclk_release_calls;
extern mpsl_clock_hfclk_src_t fake_mpsl_hfclk_src;
extern mpsl_clock_hfclk_request_callback_t fake_mpsl_hfclk_callback;
extern int32_t fake_mpsl_hfclk_request_result;
extern int32_t fake_mpsl_hfclk_release_result;
extern int32_t fake_mpsl_hfclk_is_running_result;
extern uint32_t fake_mpsl_hfclk_running;

extern unsigned fake_mpsl_radio_isr_calls;
extern unsigned fake_mpsl_rtc0_isr_calls;
extern unsigned fake_mpsl_timer0_isr_calls;
extern unsigned fake_mpsl_clock_isr_calls;

/* SoftDevice Controller */
extern unsigned fake_sdc_init_calls;
extern sdc_fault_handler_t fake_sdc_fault_handler;
extern int32_t fake_sdc_init_result;
extern unsigned fake_sdc_support_calls;
extern bool fake_sdc_support_adv_called;
extern bool fake_sdc_support_peripheral_called;
extern bool fake_sdc_support_dle_peripheral_called;
extern bool fake_sdc_support_le_2m_phy_called;
extern bool fake_sdc_support_phy_update_peripheral_called;
extern int32_t fake_sdc_support_helper_result;
extern struct fake_sdc_cfg_record fake_sdc_cfg_records[FAKE_SDC_MAX_CFG];
extern unsigned fake_sdc_cfg_count;
extern int32_t fake_sdc_cfg_required_memory;
extern int32_t fake_sdc_cfg_error;
extern const sdc_rand_source_t *fake_sdc_rand_source;
extern int32_t fake_sdc_rand_source_result;
extern unsigned fake_sdc_enable_calls;
extern sdc_callback_t fake_sdc_callback;
extern uint8_t *fake_sdc_memory;
extern int32_t fake_sdc_enable_result;

/* HCI boundary */
extern const uint8_t *fake_sdc_last_data_put;
extern unsigned fake_sdc_data_put_calls;
extern int32_t fake_sdc_data_put_result;
extern unsigned fake_sdc_get_calls;
extern int32_t fake_sdc_get_result;
extern uint8_t fake_sdc_get_type;

void fake_sdc_reset(void);

#endif /* TEST_FAKE_SDC_H */
