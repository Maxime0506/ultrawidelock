/* Subset of the pinned nrfxlib sl/include/platform/nrf_802154_platform_sl_lptimer.h. */
#ifndef TEST_NRF_802154_PLATFORM_SL_LPTIMER_H
#define TEST_NRF_802154_PLATFORM_SL_LPTIMER_H

#include <stdbool.h>
#include <stdint.h>

#define NRF_802154_SL_HW_TASK_PPI_INVALID UINT32_MAX

typedef uint32_t nrf_802154_sl_lptimer_platform_result_t;

#define NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS      0
#define NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE     1
#define NRF_802154_SL_LPTIMER_PLATFORM_TOO_DISTANT  2
#define NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES 3
#define NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE  4

void nrf_802154_platform_sl_lp_timer_init(void);
void nrf_802154_platform_sl_lp_timer_deinit(void);
uint64_t nrf_802154_platform_sl_lptimer_current_lpticks_get(void);
uint64_t nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(uint64_t us, bool round_up);
uint64_t nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(uint64_t lpticks);
void nrf_802154_platform_sl_lptimer_schedule_at(uint64_t fire_lpticks);
void nrf_802154_platform_sl_lptimer_disable(void);
void nrf_802154_platform_sl_lptimer_critical_section_enter(void);
void nrf_802154_platform_sl_lptimer_critical_section_exit(void);

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(
	uint64_t fire_lpticks, uint32_t ppi_channel);
nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void);
nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(
	uint32_t ppi_channel);

void nrf_802154_platform_sl_lptimer_sync_schedule_now(void);
void nrf_802154_platform_sl_lptimer_sync_schedule_at(uint64_t fire_lpticks);
void nrf_802154_platform_sl_lptimer_sync_abort(void);
uint32_t nrf_802154_platform_sl_lptimer_sync_event_get(void);
uint64_t nrf_802154_platform_sl_lptimer_sync_lpticks_get(void);
uint32_t nrf_802154_platform_sl_lptimer_granularity_get(void);

extern void nrf_802154_sl_timer_handler(uint64_t now_lpticks);
extern void nrf_802154_sl_timestamper_synchronized(void);

#endif /* TEST_NRF_802154_PLATFORM_SL_LPTIMER_H */
