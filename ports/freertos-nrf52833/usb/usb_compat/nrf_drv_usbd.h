/*
 * The nRF5 SDK 17.1 USB stack, on the nrfx this port actually links.
 *
 * app_usbd and the CDC ACM class were written against the nrfx bundled with
 * nRF5 SDK 17.1 (nrfx 1.9.0) and reach the driver through this legacy header.
 * This image links a much newer nrfx -- the one hal_nordic ships and the radio
 * stack is built against -- because two register maps in one image is the
 * failure this port refuses to have. So one of the two has to bend, and it is
 * this header rather than either tree.
 *
 * WHAT ACTUALLY DIFFERS, measured rather than assumed: between nrfx 1.9.0 and
 * 4.2.1 the nrfx_usbd API is identical apart from three functions whose return
 * type changed from nrfx_err_t to int, one added function, and NRFX_USBD_ISOSIZE
 * moving 1024 -> 1023. This image has no isochronous endpoint, so the constant
 * is not reachable. That leaves the three return types, which are wrapped below
 * and are the entire content of the incompatibility.
 *
 * Everything else is the SDK's own mapping, reproduced rather than included:
 * the SDK header #defines the three wrapped names straight through, so it
 * cannot be included alongside these wrappers.
 *
 * The translation is one way and lossy on purpose. The SDK's callers compare
 * against NRF_SUCCESS and propagate anything else outward as a ret_code_t;
 * nrfx now returns negative errno. Mapping the specific codes back would invent
 * a precision neither side has -- what the callers need is that success is
 * success, and that a failure is not mistaken for one.
 */
#ifndef NRF_DRV_USBD_H__
#define NRF_DRV_USBD_H__

#include "nrfx.h"
#include "nrfx_usbd.h"
#include "sdk_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NRF_DRV_USBD_DMASCHEDULER_PRIORITIZED    NRFX_USBD_DMASCHEDULER_PRIORITIZED
#define NRF_DRV_USBD_DMASCHEDULER_ROUNDROBIN     NRFX_USBD_DMASCHEDULER_ROUNDROBIN
#define NRF_DRV_USBD_EPSIZE                      NRFX_USBD_EPSIZE
#define NRF_DRV_USBD_ISOSIZE                     NRFX_USBD_ISOSIZE
#define NRF_DRV_USBD_FEEDER_BUFFER_SIZE          NRFX_USBD_EPSIZE
#define NRF_DRV_USBD_EPIN                        NRFX_USBD_EPIN
#define NRF_DRV_USBD_EPOUT                       NRFX_USBD_EPOUT
typedef nrfx_usbd_ep_t                     nrf_drv_usbd_ep_t;
#define NRF_DRV_USBD_EPOUT0                      NRFX_USBD_EPOUT0
#define NRF_DRV_USBD_EPOUT1                      NRFX_USBD_EPOUT1
#define NRF_DRV_USBD_EPOUT2                      NRFX_USBD_EPOUT2
#define NRF_DRV_USBD_EPOUT3                      NRFX_USBD_EPOUT3
#define NRF_DRV_USBD_EPOUT4                      NRFX_USBD_EPOUT4
#define NRF_DRV_USBD_EPOUT5                      NRFX_USBD_EPOUT5
#define NRF_DRV_USBD_EPOUT6                      NRFX_USBD_EPOUT6
#define NRF_DRV_USBD_EPOUT7                      NRFX_USBD_EPOUT7
#define NRF_DRV_USBD_EPOUT8                      NRFX_USBD_EPOUT8
#define NRF_DRV_USBD_EPIN0                       NRFX_USBD_EPIN0
#define NRF_DRV_USBD_EPIN1                       NRFX_USBD_EPIN1
#define NRF_DRV_USBD_EPIN2                       NRFX_USBD_EPIN2
#define NRF_DRV_USBD_EPIN3                       NRFX_USBD_EPIN3
#define NRF_DRV_USBD_EPIN4                       NRFX_USBD_EPIN4
#define NRF_DRV_USBD_EPIN5                       NRFX_USBD_EPIN5
#define NRF_DRV_USBD_EPIN6                       NRFX_USBD_EPIN6
#define NRF_DRV_USBD_EPIN7                       NRFX_USBD_EPIN7
#define NRF_DRV_USBD_EPIN8                       NRFX_USBD_EPIN8
typedef nrfx_usbd_event_type_t             nrf_drv_usbd_event_type_t;
#define NRF_DRV_USBD_EVT_SOF                     NRFX_USBD_EVT_SOF
#define NRF_DRV_USBD_EVT_RESET                   NRFX_USBD_EVT_RESET
#define NRF_DRV_USBD_EVT_SUSPEND                 NRFX_USBD_EVT_SUSPEND
#define NRF_DRV_USBD_EVT_RESUME                  NRFX_USBD_EVT_RESUME
#define NRF_DRV_USBD_EVT_WUREQ                   NRFX_USBD_EVT_WUREQ
#define NRF_DRV_USBD_EVT_SETUP                   NRFX_USBD_EVT_SETUP
#define NRF_DRV_USBD_EVT_EPTRANSFER              NRFX_USBD_EVT_EPTRANSFER
#define NRF_DRV_USBD_EVT_CNT                     NRFX_USBD_EVT_CNT
#define NRF_USBD_EP_OK                           NRFX_USBD_EP_OK
#define NRF_USBD_EP_WAITING                      NRFX_USBD_EP_WAITING
#define NRF_USBD_EP_OVERLOAD                     NRFX_USBD_EP_OVERLOAD
#define NRF_USBD_EP_ABORTED                      NRFX_USBD_EP_ABORTED
typedef nrfx_usbd_ep_status_t              nrf_drv_usbd_ep_status_t;
typedef nrfx_usbd_evt_t                    nrf_drv_usbd_evt_t;
typedef nrfx_usbd_event_handler_t          nrf_drv_usbd_event_handler_t;
typedef nrfx_usbd_data_ptr_t               nrf_drv_usbd_data_ptr_t;
typedef nrfx_usbd_ep_transfer_t            nrf_drv_usbd_ep_transfer_t;
typedef nrfx_usbd_transfer_flags_t         nrf_drv_usbd_transfer_flags_t;
#define NRF_DRV_USBD_TRANSFER_ZLP_FLAG           NRFX_USBD_TRANSFER_ZLP_FLAG
typedef nrfx_usbd_transfer_t               nrf_drv_usbd_transfer_t;
#define NRF_DRV_USBD_TRANSFER_IN_FLAGS(name, tx_buff, tx_size, tx_flags) \
                NRFX_USBD_TRANSFER_IN(name, tx_buff, tx_size, tx_flags)
#define NRF_DRV_USBD_TRANSFER_IN(name, tx_buff, tx_size) \
                NRFX_USBD_TRANSFER_IN(name, tx_buff, tx_size, 0)
#define NRF_DRV_USBD_TRANSFER_IN_ZLP(name, tx_buff, tx_size) \
                NRFX_USBD_TRANSFER_IN(name, tx_buff, tx_size, NRFX_USBD_TRANSFER_ZLP_FLAG)
#define NRF_DRV_USBD_TRANSFER_OUT                NRFX_USBD_TRANSFER_OUT
typedef nrfx_usbd_feeder_t                 nrf_drv_usbd_feeder_t;
typedef nrfx_usbd_consumer_t               nrf_drv_usbd_consumer_t;
typedef nrfx_usbd_handler_t                nrf_drv_usbd_handler_t;
typedef nrfx_usbd_handler_desc_t           nrf_drv_usbd_handler_desc_t;
typedef nrfx_usbd_setup_t                  nrf_drv_usbd_setup_t;
#define nrf_drv_usbd_enable                      nrfx_usbd_enable
#define nrf_drv_usbd_disable                     nrfx_usbd_disable
#define nrf_drv_usbd_start                       nrfx_usbd_start
#define nrf_drv_usbd_stop                        nrfx_usbd_stop
#define nrf_drv_usbd_is_initialized              nrfx_usbd_is_initialized
#define nrf_drv_usbd_is_enabled                  nrfx_usbd_is_enabled
#define nrf_drv_usbd_is_started                  nrfx_usbd_is_started
#define nrf_drv_usbd_suspend                     nrfx_usbd_suspend
#define nrf_drv_usbd_wakeup_req                  nrfx_usbd_wakeup_req
#define nrf_drv_usbd_suspend_check               nrfx_usbd_suspend_check
#define nrf_drv_usbd_suspend_irq_config          nrfx_usbd_suspend_irq_config
#define nrf_drv_usbd_active_irq_config           nrfx_usbd_active_irq_config
#define nrf_drv_usbd_force_bus_wakeup            nrfx_usbd_force_bus_wakeup
#define nrf_drv_usbd_bus_suspend_check           nrfx_usbd_bus_suspend_check
#define nrf_drv_usbd_ep_max_packet_size_set      nrfx_usbd_ep_max_packet_size_set
#define nrf_drv_usbd_ep_max_packet_size_get      nrfx_usbd_ep_max_packet_size_get
#define nrf_drv_usbd_ep_enable_check             nrfx_usbd_ep_enable_check
#define nrf_drv_usbd_ep_enable                   nrfx_usbd_ep_enable
#define nrf_drv_usbd_ep_disable                  nrfx_usbd_ep_disable
#define nrf_drv_usbd_ep_default_config           nrfx_usbd_ep_default_config
#define nrf_drv_usbd_feeder_buffer_get           nrfx_usbd_feeder_buffer_get
#define nrf_drv_usbd_ep_status_get               nrfx_usbd_ep_status_get
#define nrf_drv_usbd_epout_size_get              nrfx_usbd_epout_size_get
#define nrf_drv_usbd_ep_is_busy                  nrfx_usbd_ep_is_busy
#define nrf_drv_usbd_ep_stall                    nrfx_usbd_ep_stall
#define nrf_drv_usbd_ep_stall_clear              nrfx_usbd_ep_stall_clear
#define nrf_drv_usbd_ep_stall_check              nrfx_usbd_ep_stall_check
#define nrf_drv_usbd_ep_dtoggle_clear            nrfx_usbd_ep_dtoggle_clear
#define nrf_drv_usbd_setup_get                   nrfx_usbd_setup_get
#define nrf_drv_usbd_setup_data_clear            nrfx_usbd_setup_data_clear
#define nrf_drv_usbd_setup_clear                 nrfx_usbd_setup_clear
#define nrf_drv_usbd_setup_stall                 nrfx_usbd_setup_stall
#define nrf_drv_usbd_ep_abort                    nrfx_usbd_ep_abort
#define nrf_drv_usbd_last_setup_dir_get          nrfx_usbd_last_setup_dir_get
#define nrf_drv_usbd_transfer_out_drop           nrfx_usbd_transfer_out_drop

/*
 * The three that moved. Each returns 0 on success in both nrfx versions, so the
 * success path needs no translation; the failure path is collapsed to a single
 * code, because nrfx's negative errno and the SDK's NRF_ERROR_* space do not
 * correspond and pretending otherwise would put a wrong reason in a log.
 */
static inline ret_code_t woz_usbd_ret(int rc)
{
	return (rc == 0) ? NRF_SUCCESS : NRF_ERROR_INTERNAL;
}

static inline ret_code_t nrf_drv_usbd_init(nrfx_usbd_event_handler_t event_handler)
{
	return woz_usbd_ret(nrfx_usbd_init(event_handler));
}

static inline ret_code_t nrf_drv_usbd_ep_transfer(nrfx_usbd_ep_t ep,
						  nrfx_usbd_transfer_t const *p_transfer)
{
	return woz_usbd_ret(nrfx_usbd_ep_transfer(ep, p_transfer));
}

static inline ret_code_t
nrf_drv_usbd_ep_handled_transfer(nrfx_usbd_ep_t ep, nrfx_usbd_handler_desc_t const *p_handler)
{
	return woz_usbd_ret(nrfx_usbd_ep_handled_transfer(ep, p_handler));
}

static inline ret_code_t nrf_drv_usbd_uninit(void)
{
	nrfx_usbd_uninit();
	return NRF_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif /* NRF_DRV_USBD_H__ */
