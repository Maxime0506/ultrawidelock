#include "woz_freertos_radio.h"

#include <stddef.h>

#include "woz_freertos_mpsl.h"
#include "woz_freertos_nimble_sdc.h"
#include "woz_freertos_platform.h"

#include <mpsl.h>
#include <mpsl_clock.h>
#include <nrf_errno.h>
#include <nrfx.h>
#include <sdc.h>
#include <sdc_hci.h>
#include <sdc_soc.h>

/*
 * Interrupt priorities are frozen in peripherals.yml. MPSL owns RADIO, RTC0,
 * TIMER0, and the CLOCK half of POWER_CLOCK at priority 0. SWI5_EGU5 carries
 * the MPSL low-priority signal and must stay callable from FreeRTOS.
 */
#define WOZ_FREERTOS_RADIO_MPSL_IRQ_PRIORITY 0u
#define WOZ_FREERTOS_RADIO_LOW_PRIORITY_IRQ_PRIORITY 4u

/*
 * The one place both halves of the FromISR rule are visible at once.
 *
 * woz_freertos_radio_low_priority_isr runs at the priority above and calls
 * woz_freertos_mpsl_wake_from_isr, which is vTaskNotifyGiveFromISR followed by
 * portYIELD_FROM_ISR. FreeRTOS permits that only from a handler at or
 * numerically below configMAX_SYSCALL_INTERRUPT_PRIORITY. Stating it here means
 * moving either number without the other is a build failure rather than an
 * assert on the bench, or worse, a race in a release build.
 *
 * The reverse bound -- that the ceiling stays above MPSL at 0 and nRF 802.15.4
 * at 1, so no critical section can mask them -- is asserted in
 * board/FreeRTOSConfig.h, where those two numbers are the ones in view.
 */
#if defined(configMAX_SYSCALL_INTERRUPT_PRIORITY)
_Static_assert(WOZ_FREERTOS_RADIO_LOW_PRIORITY_IRQ_PRIORITY >=
		       configMAX_SYSCALL_INTERRUPT_PRIORITY,
	       "the MPSL low-priority handler calls FreeRTOS FromISR APIs and must not run "
	       "above configMAX_SYSCALL_INTERRUPT_PRIORITY");
#endif

/*
 * The DWM3001CDK runs its low-frequency clock from the module crystal, which
 * is what the Zephyr oracle selects (Zephyr defaults nRF52 to
 * CLOCK_CONTROL_NRF_K32SRC_XTAL at 50 ppm and the board does not override it).
 */
#ifndef WOZ_FREERTOS_RADIO_LFCLK_ACCURACY_PPM
#define WOZ_FREERTOS_RADIO_LFCLK_ACCURACY_PPM 50u
#endif

/*
 * One peripheral link at the full 251-byte Link Layer packet size, one legacy
 * advertising set, and the default Filter Accept List need 3078 bytes with the
 * pinned SDC_MEM_* macros; the controller itself asked for 2,864
 * (s_sdc_memory_used, read off the hardware 2026-08-14). The pool keeps
 * headroom for a controller update and the start fails loudly if the
 * controller ever asks for more.
 */
#ifndef WOZ_FREERTOS_SDC_MEM_BYTES
#define WOZ_FREERTOS_SDC_MEM_BYTES 3328u
#endif

#ifndef WOZ_FREERTOS_SDC_TX_PACKET_SIZE
#define WOZ_FREERTOS_SDC_TX_PACKET_SIZE 251u
#endif
#ifndef WOZ_FREERTOS_SDC_RX_PACKET_SIZE
#define WOZ_FREERTOS_SDC_RX_PACKET_SIZE 251u
#endif
#ifndef WOZ_FREERTOS_SDC_TX_PACKET_COUNT
#define WOZ_FREERTOS_SDC_TX_PACKET_COUNT 3u
#endif
#ifndef WOZ_FREERTOS_SDC_RX_PACKET_COUNT
#define WOZ_FREERTOS_SDC_RX_PACKET_COUNT 3u
#endif

#define WOZ_FREERTOS_RADIO_TAG "radio"

static uint8_t s_sdc_memory[WOZ_FREERTOS_SDC_MEM_BYTES] __attribute__((aligned(8)));
static uint32_t s_sdc_memory_used;
static bool s_ready;

void woz_freertos_radio_radio_isr(void)
{
	MPSL_IRQ_RADIO_Handler();
}

void woz_freertos_radio_rtc0_isr(void)
{
	MPSL_IRQ_RTC0_Handler();
}

void woz_freertos_radio_timer0_isr(void)
{
	MPSL_IRQ_TIMER0_Handler();
}

/*
 * The POWER half of this vector, which MPSL does not read.
 *
 * MPSL_IRQ_CLOCK_Handler services the CLOCK events and leaves the POWER ones
 * latched -- USBDETECTED, USBREMOVED and USBPWRRDY are not its business. Left
 * unread and unmasked they would re-enter this vector at priority 0 forever, so
 * whoever enables them also has to consume them, and that is the USB stack.
 */
static woz_freertos_power_handler s_power_handler;

int woz_freertos_radio_set_power_handler(woz_freertos_power_handler fn)
{
	if (fn == NULL || s_power_handler != NULL) {
		return -1;
	}
	s_power_handler = fn;
	return 0;
}

void woz_freertos_radio_power_clock_isr(void)
{
	/* MPSL first, unconditionally. Its events are the ones with a deadline,
	 * and the POWER events are a cable being plugged in. */
	MPSL_IRQ_CLOCK_Handler();

	if (s_power_handler != NULL) {
		s_power_handler();
	}
}

void woz_freertos_radio_low_priority_isr(void)
{
	woz_freertos_mpsl_wake_from_isr();
}

bool woz_freertos_radio_ready(void)
{
	return s_ready;
}

uint32_t woz_freertos_radio_memory_used(void)
{
	return s_sdc_memory_used;
}

static void mpsl_assert(const char *file, const uint32_t line)
{
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_RADIO_TAG,
			 "MPSL assert at %s:%u", file != NULL ? file : "?",
			 (unsigned)line);
	woz_freertos_fatal("mpsl assert");
}

static void sdc_fault(const char *file, const uint32_t line)
{
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_RADIO_TAG,
			 "SoftDevice Controller fault at %s:%u",
			 file != NULL ? file : "?", (unsigned)line);
	woz_freertos_fatal("sdc fault");
}

/*
 * The controller calls this from the mpsl_low_priority_process context, so it
 * already runs on the MPSL worker task and only has to wake the HCI receiver.
 */
static void sdc_host_signal(void)
{
	woz_freertos_nimble_sdc_wake();
}

/*
 * Also called from the mpsl_low_priority_process context, and required to
 * block until the full length is written.
 */
static void sdc_rand_poll(uint8_t *buffer, uint8_t length)
{
	if (woz_freertos_entropy(buffer, length) != 0) {
		woz_freertos_fatal("sdc entropy");
	}
}

static void transport_fault(enum woz_freertos_nimble_sdc_fault fault, int32_t detail)
{
	woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_RADIO_TAG,
			 "HCI transport fault %d detail %d", (int)fault, (int)detail);
	woz_freertos_fatal("hci transport");
}

/*
 * ACL data has no opcode to decode, so it bypasses the dispatcher and goes
 * straight to the controller.
 */
static int32_t sdc_data_put(const uint8_t *packet)
{
	return sdc_hci_data_put(packet);
}

/* Only what a peripheral GATT plus L2CAP CoC product needs is linked in. */
static int32_t enable_supported_features(void)
{
	int32_t rc;

	rc = sdc_support_helper(sdc_support_adv);
	if (rc != 0) {
		return rc;
	}
	rc = sdc_support_helper(sdc_support_peripheral);
	if (rc != 0) {
		return rc;
	}
	rc = sdc_support_helper(sdc_support_dle_peripheral);
	if (rc != 0) {
		return rc;
	}
	rc = sdc_support_helper(sdc_support_le_2m_phy);
	if (rc != 0) {
		return rc;
	}
	return sdc_support_helper(sdc_support_phy_update_peripheral);
}

/* Returns the required controller memory in bytes, or a negative nrf_errno. */
static int32_t apply_resource_cfg(void)
{
	sdc_cfg_t cfg;
	int32_t rc;

	/*
	 * SDC_CFG_TYPE_CENTRAL_COUNT is deliberately NOT set, and zero is not a
	 * safe value to pass -- it is not a value at all here.
	 *
	 * This image links libsoftdevice_controller_peripheral.a, which has no
	 * central role to size, and it rejects the config TYPE outright rather
	 * than accepting a count of zero: the call returns -NRF_EOPNOTSUPP (-45)
	 * and startup fails at STAGE_SDC_CFG. Setting it to zero reads as the
	 * careful thing to do, which is exactly why this note is here.
	 *
	 * Found on hardware. Nothing in the host suite could see it: the
	 * controller is a real vendor archive with a real role restriction, and
	 * the fake accepts every configuration it is handed.
	 */
	cfg.peripheral_count.count = 1;
	rc = sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG, SDC_CFG_TYPE_PERIPHERAL_COUNT, &cfg);
	if (rc < 0) {
		return rc;
	}

	cfg.adv_count.count = 1;
	rc = sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG, SDC_CFG_TYPE_ADV_COUNT, &cfg);
	if (rc < 0) {
		return rc;
	}

	cfg.buffer_cfg.tx_packet_size = WOZ_FREERTOS_SDC_TX_PACKET_SIZE;
	cfg.buffer_cfg.rx_packet_size = WOZ_FREERTOS_SDC_RX_PACKET_SIZE;
	cfg.buffer_cfg.tx_packet_count = WOZ_FREERTOS_SDC_TX_PACKET_COUNT;
	cfg.buffer_cfg.rx_packet_count = WOZ_FREERTOS_SDC_RX_PACKET_COUNT;
	return sdc_cfg_set(SDC_DEFAULT_RESOURCE_CFG_TAG, SDC_CFG_TYPE_BUFFER_CFG, &cfg);
}

static void set_interrupt_priorities(void)
{
	NVIC_SetPriority(RADIO_IRQn, WOZ_FREERTOS_RADIO_MPSL_IRQ_PRIORITY);
	NVIC_SetPriority(RTC0_IRQn, WOZ_FREERTOS_RADIO_MPSL_IRQ_PRIORITY);
	NVIC_SetPriority(TIMER0_IRQn, WOZ_FREERTOS_RADIO_MPSL_IRQ_PRIORITY);
	NVIC_SetPriority(POWER_CLOCK_IRQn, WOZ_FREERTOS_RADIO_MPSL_IRQ_PRIORITY);
	NVIC_SetPriority(SWI5_EGU5_IRQn, WOZ_FREERTOS_RADIO_LOW_PRIORITY_IRQ_PRIORITY);
}

/* The oracle enables the MPSL vectors only after mpsl_init() returns, so a
 * failed init cannot route an interrupt into an uninitialized MPSL. */
static void enable_interrupts(void)
{
	NVIC_EnableIRQ(RADIO_IRQn);
	NVIC_EnableIRQ(RTC0_IRQn);
	NVIC_EnableIRQ(TIMER0_IRQn);
	NVIC_EnableIRQ(POWER_CLOCK_IRQn);
	NVIC_EnableIRQ(SWI5_EGU5_IRQn);
}

int woz_freertos_radio_start(const struct woz_freertos_radio_dispatcher *dispatcher)
{
	static const mpsl_clock_lfclk_cfg_t clock_cfg = {
		.source = MPSL_CLOCK_LF_SRC_XTAL,
		.rc_ctiv = 0,
		.rc_temp_ctiv = 0,
		.accuracy_ppm = WOZ_FREERTOS_RADIO_LFCLK_ACCURACY_PPM,
		.skip_wait_lfclk_started = false,
	};
	static const sdc_rand_source_t rand_source = {
		.rand_poll = sdc_rand_poll,
	};
	struct woz_freertos_nimble_sdc_ops ops;
	int32_t rc;

	if (dispatcher == NULL || dispatcher->cmd_put == NULL ||
	    dispatcher->msg_get == NULL) {
		return -WOZ_FREERTOS_RADIO_STAGE_TRANSPORT;
	}
	if (s_ready) {
		return 0;
	}

	set_interrupt_priorities();

	if (mpsl_init(&clock_cfg, SWI5_EGU5_IRQn, mpsl_assert) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_MPSL_INIT;
	}
	enable_interrupts();
	if (woz_freertos_mpsl_start(mpsl_low_priority_process) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_MPSL_WORKER;
	}
	if (sdc_init(sdc_fault) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_INIT;
	}
	if (enable_supported_features() != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_SUPPORT;
	}

	rc = apply_resource_cfg();
	if (rc < 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_CFG;
	}
	if ((uint32_t)rc > sizeof(s_sdc_memory)) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, WOZ_FREERTOS_RADIO_TAG,
				 "SoftDevice Controller needs %u bytes, pool is %u",
				 (unsigned)rc, (unsigned)sizeof(s_sdc_memory));
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_MEMORY;
	}

	if (sdc_rand_source_register(&rand_source) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_RAND;
	}
	if (sdc_enable(sdc_host_signal, s_sdc_memory) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_SDC_ENABLE;
	}

	ops.cmd_put = dispatcher->cmd_put;
	ops.data_put = sdc_data_put;
	ops.msg_get = dispatcher->msg_get;
	ops.fault = transport_fault;
	ops.no_data_error = -NRF_EAGAIN;
	if (woz_freertos_nimble_sdc_configure(&ops) != 0) {
		return -WOZ_FREERTOS_RADIO_STAGE_TRANSPORT;
	}

	s_sdc_memory_used = (uint32_t)rc;
	s_ready = true;
	return 0;
}
