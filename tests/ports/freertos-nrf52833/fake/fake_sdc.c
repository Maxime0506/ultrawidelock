#include <string.h>

#include "fake_sdc.h"

#include <nrf_errno.h>
#include <sdc_hci.h>

unsigned fake_mpsl_init_calls;
unsigned fake_mpsl_hfclk_request_calls;
unsigned fake_mpsl_hfclk_release_calls;
mpsl_clock_hfclk_src_t fake_mpsl_hfclk_src;
mpsl_clock_hfclk_request_callback_t fake_mpsl_hfclk_callback;
int32_t fake_mpsl_hfclk_request_result;
int32_t fake_mpsl_hfclk_release_result;
int32_t fake_mpsl_hfclk_is_running_result;
uint32_t fake_mpsl_hfclk_running;
mpsl_clock_lfclk_cfg_t fake_mpsl_clock_cfg;
IRQn_Type fake_mpsl_low_prio_irq;
mpsl_assert_handler_t fake_mpsl_assert_handler;
int32_t fake_mpsl_init_result;
unsigned fake_mpsl_low_priority_process_calls;
unsigned fake_mpsl_radio_isr_calls;
unsigned fake_mpsl_rtc0_isr_calls;
unsigned fake_mpsl_timer0_isr_calls;
unsigned fake_mpsl_clock_isr_calls;

unsigned fake_sdc_init_calls;
sdc_fault_handler_t fake_sdc_fault_handler;
int32_t fake_sdc_init_result;
unsigned fake_sdc_support_calls;
bool fake_sdc_support_adv_called;
bool fake_sdc_support_peripheral_called;
bool fake_sdc_support_dle_peripheral_called;
bool fake_sdc_support_le_2m_phy_called;
bool fake_sdc_support_phy_update_peripheral_called;
int32_t fake_sdc_support_helper_result;
struct fake_sdc_cfg_record fake_sdc_cfg_records[FAKE_SDC_MAX_CFG];
unsigned fake_sdc_cfg_count;
int32_t fake_sdc_cfg_required_memory;
int32_t fake_sdc_cfg_error;
const sdc_rand_source_t *fake_sdc_rand_source;
int32_t fake_sdc_rand_source_result;
unsigned fake_sdc_enable_calls;
sdc_callback_t fake_sdc_callback;
uint8_t *fake_sdc_memory;
int32_t fake_sdc_enable_result;

const uint8_t *fake_sdc_last_data_put;
unsigned fake_sdc_data_put_calls;
int32_t fake_sdc_data_put_result;
unsigned fake_sdc_get_calls;
int32_t fake_sdc_get_result;
uint8_t fake_sdc_get_type;

void fake_sdc_reset(void)
{
	fake_mpsl_init_calls = 0;
	fake_mpsl_hfclk_request_calls = 0;
	fake_mpsl_hfclk_release_calls = 0;
	fake_mpsl_hfclk_src = MPSL_CLOCK_HF_SRC_MAX;
	fake_mpsl_hfclk_callback = NULL;
	fake_mpsl_hfclk_request_result = 0;
	fake_mpsl_hfclk_release_result = 0;
	fake_mpsl_hfclk_is_running_result = 0;
	fake_mpsl_hfclk_running = 0;
	memset(&fake_mpsl_clock_cfg, 0, sizeof(fake_mpsl_clock_cfg));
	fake_mpsl_low_prio_irq = 0;
	fake_mpsl_assert_handler = NULL;
	fake_mpsl_init_result = 0;
	fake_mpsl_low_priority_process_calls = 0;
	fake_mpsl_radio_isr_calls = 0;
	fake_mpsl_rtc0_isr_calls = 0;
	fake_mpsl_timer0_isr_calls = 0;
	fake_mpsl_clock_isr_calls = 0;

	fake_sdc_init_calls = 0;
	fake_sdc_fault_handler = NULL;
	fake_sdc_init_result = 0;
	fake_sdc_support_calls = 0;
	fake_sdc_support_adv_called = false;
	fake_sdc_support_peripheral_called = false;
	fake_sdc_support_dle_peripheral_called = false;
	fake_sdc_support_le_2m_phy_called = false;
	fake_sdc_support_phy_update_peripheral_called = false;
	fake_sdc_support_helper_result = 0;
	memset(fake_sdc_cfg_records, 0, sizeof(fake_sdc_cfg_records));
	fake_sdc_cfg_count = 0;
	fake_sdc_cfg_required_memory = 3078;
	fake_sdc_cfg_error = 0;
	fake_sdc_rand_source = NULL;
	fake_sdc_rand_source_result = 0;
	fake_sdc_enable_calls = 0;
	fake_sdc_callback = NULL;
	fake_sdc_memory = NULL;
	fake_sdc_enable_result = 0;

	fake_sdc_last_data_put = NULL;
	fake_sdc_data_put_calls = 0;
	fake_sdc_data_put_result = 0;
	fake_sdc_get_calls = 0;
	fake_sdc_get_result = -NRF_EAGAIN;
	fake_sdc_get_type = SDC_HCI_MSG_TYPE_NONE;
}

int32_t mpsl_init(mpsl_clock_lfclk_cfg_t const *p_clock_config, IRQn_Type low_prio_irq,
		  mpsl_assert_handler_t p_assert_handler)
{
	fake_mpsl_init_calls++;
	if (p_clock_config != NULL) {
		fake_mpsl_clock_cfg = *p_clock_config;
	}
	fake_mpsl_low_prio_irq = low_prio_irq;
	fake_mpsl_assert_handler = p_assert_handler;
	return fake_mpsl_init_result;
}

void mpsl_uninit(void)
{
}

void mpsl_low_priority_process(void)
{
	fake_mpsl_low_priority_process_calls++;
}

void MPSL_IRQ_RADIO_Handler(void)
{
	fake_mpsl_radio_isr_calls++;
}

void MPSL_IRQ_RTC0_Handler(void)
{
	fake_mpsl_rtc0_isr_calls++;
}

void MPSL_IRQ_TIMER0_Handler(void)
{
	fake_mpsl_timer0_isr_calls++;
}

void MPSL_IRQ_CLOCK_Handler(void)
{
	fake_mpsl_clock_isr_calls++;
}

int32_t sdc_init(sdc_fault_handler_t fault_handler)
{
	fake_sdc_init_calls++;
	fake_sdc_fault_handler = fault_handler;
	return fake_sdc_init_result;
}

int32_t sdc_support_helper(void (*sdc_support_func)(void))
{
	fake_sdc_support_calls++;
	if (fake_sdc_support_helper_result != 0) {
		return fake_sdc_support_helper_result;
	}
	sdc_support_func();
	return 0;
}

void sdc_support_adv(void)
{
	fake_sdc_support_adv_called = true;
}

void sdc_support_peripheral(void)
{
	fake_sdc_support_peripheral_called = true;
}

void sdc_support_dle_peripheral(void)
{
	fake_sdc_support_dle_peripheral_called = true;
}

void sdc_support_le_2m_phy(void)
{
	fake_sdc_support_le_2m_phy_called = true;
}

void sdc_support_phy_update_peripheral(void)
{
	fake_sdc_support_phy_update_peripheral_called = true;
}

int32_t sdc_cfg_set(uint8_t config_tag, uint8_t config_type, sdc_cfg_t const *p_resource_cfg)
{
	if (fake_sdc_cfg_count < FAKE_SDC_MAX_CFG) {
		fake_sdc_cfg_records[fake_sdc_cfg_count].tag = config_tag;
		fake_sdc_cfg_records[fake_sdc_cfg_count].type = config_type;
		if (p_resource_cfg != NULL) {
			fake_sdc_cfg_records[fake_sdc_cfg_count].cfg = *p_resource_cfg;
		}
	}
	fake_sdc_cfg_count++;
	if (fake_sdc_cfg_error != 0) {
		return fake_sdc_cfg_error;
	}
	return fake_sdc_cfg_required_memory;
}

int32_t sdc_rand_source_register(const sdc_rand_source_t *rand_source)
{
	fake_sdc_rand_source = rand_source;
	return fake_sdc_rand_source_result;
}

int32_t sdc_enable(sdc_callback_t callback, uint8_t *p_mem)
{
	fake_sdc_enable_calls++;
	if (fake_sdc_enable_result != 0) {
		return fake_sdc_enable_result;
	}
	fake_sdc_callback = callback;
	fake_sdc_memory = p_mem;
	return 0;
}

int32_t sdc_disable(void)
{
	return 0;
}

int32_t sdc_hci_data_put(uint8_t const *p_data_in)
{
	fake_sdc_data_put_calls++;
	fake_sdc_last_data_put = p_data_in;
	return fake_sdc_data_put_result;
}

int32_t sdc_hci_get(uint8_t *p_packet_out, uint8_t *p_msg_type_out)
{
	fake_sdc_get_calls++;
	(void)p_packet_out;
	if (p_msg_type_out != NULL) {
		*p_msg_type_out = fake_sdc_get_type;
	}
	return fake_sdc_get_result;
}

int32_t mpsl_clock_hfclk_src_request(mpsl_clock_hfclk_src_t src,
				     mpsl_clock_hfclk_request_callback_t hfclk_started_callback)
{
	fake_mpsl_hfclk_request_calls++;
	fake_mpsl_hfclk_src = src;
	fake_mpsl_hfclk_callback = hfclk_started_callback;
	return fake_mpsl_hfclk_request_result;
}

int32_t mpsl_clock_hfclk_src_release(mpsl_clock_hfclk_src_t src)
{
	fake_mpsl_hfclk_release_calls++;
	fake_mpsl_hfclk_src = src;
	return fake_mpsl_hfclk_release_result;
}

int32_t mpsl_clock_hfclk_src_is_running(mpsl_clock_hfclk_src_t src, uint32_t *p_is_running)
{
	(void)src;
	if (p_is_running != NULL) {
		*p_is_running = fake_mpsl_hfclk_running;
	}
	return fake_mpsl_hfclk_is_running_result;
}
