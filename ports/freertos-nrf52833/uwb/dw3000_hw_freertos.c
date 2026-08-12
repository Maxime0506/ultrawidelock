/*
 * The DW3110's reset, interrupt and wake lines, for the standalone FreeRTOS
 * port. This implements modules/woz_dw3000's dw3000_hw.h; modules/ does not
 * change to accommodate it.
 *
 * WHY THE INTERRUPT WAKES A TASK. dwt_isr() reads the chip over SPI and then
 * calls the ranging callbacks, which run the DS-TWR choreography and touch
 * crypto. None of that can happen in an interrupt handler, so the GPIOTE vector
 * does one thing: notify a dedicated task. The task then calls dwt_isr in a
 * loop for as long as the line is still asserted, because the DW3110 holds it
 * high until every pending event has been read out and a single edge can stand
 * for several.
 *
 * WHY THE VECTOR SITS AT PRIORITY 4. Four is the most urgent level at which a
 * handler may still call vTaskNotifyGiveFromISR: FreeRTOS permits its FromISR
 * API only at or below configMAX_SYSCALL_INTERRUPT_PRIORITY, which this port
 * pins at 4 so that MPSL's own low-priority handler is legal there too. Five
 * would also be legal and is the obvious choice, and it is the wrong one: at
 * five, MPSL's low-priority work on SWI5_EGU5 preempts this line, and the gate
 * for the whole port is the ~1.836 ms deadline between the DW3110 raising an
 * event and the response being armed. At four the two are equal and neither can
 * interrupt the other, which costs MPSL the few hundred nanoseconds this
 * handler takes to notify and return. peripherals.yml records the choice.
 *
 * The reset line is driven low to assert and released to high impedance rather
 * than driven high, because the DW3110 expects an open-drain reset and the
 * module carries the pull-up.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_gpiote.h>
#include <nrfx.h>

#include <FreeRTOS.h>
#include <task.h>

#include <woz_freertos_board.h>
#include <woz_freertos_platform.h>

#include "board_pins.h"
#include "deca_device_api.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"

#define TAG "dw3000_hw"

/* WOZ_DW3000_GPIOTE_CHANNEL is in board_pins.h with the rest of the peripheral
 * claims, and is asserted against the radio stacks in
 * radio/peripheral_asserts_freertos.c. */

/* Frozen in peripherals.yml; see the note above for why it is 4 and not 5. */
#ifndef WOZ_DW3000_IRQ_PRIORITY
#define WOZ_DW3000_IRQ_PRIORITY 4u
#endif

/*
 * Stated here because nothing else will state it. FreeRTOS permits its FromISR
 * API only from handlers at or below the syscall ceiling, and this handler
 * calls one -- but vPortValidateInterruptPriority cannot catch a violation on
 * this kernel: port_cmsis.c builds its threshold as (ceiling & 0xe0), and 4 and
 * 0xe0 have no bits in common, so the check reads zero and never fires. A
 * handler moved above the ceiling would therefore assert nothing and simply
 * race the scheduler's ready lists now and then. This assertion is the whole
 * protection, so it fails the build instead.
 */
_Static_assert(WOZ_DW3000_IRQ_PRIORITY >= configMAX_SYSCALL_INTERRUPT_PRIORITY,
	       "the DW3110 vector calls a FreeRTOS FromISR API, so it must sit at or below "
	       "configMAX_SYSCALL_INTERRUPT_PRIORITY");

/*
 * Deep enough for dwt_isr and everything it calls: the RX shim, the DS-TWR
 * choreography and the CCC key derivation underneath it. Carried over from the
 * ESP-IDF port, where this worker runs the same call tree; the true figure is a
 * first-link measurement like the rest of this port's stack sizes.
 */
#ifndef WOZ_DW3000_ISR_TASK_STACK
#define WOZ_DW3000_ISR_TASK_STACK 4096u
#endif

/*
 * Above the application and the Thread task, below nothing that matters. The
 * ESP-IDF port measured what happens when this is merely high rather than
 * highest: other work preempted the worker for about 2.4 ms per TX-done, so
 * the Final arm always ran after the Final had already passed.
 */
#ifndef WOZ_DW3000_ISR_TASK_PRIORITY
#define WOZ_DW3000_ISR_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#endif

/*
 * Cycle counters the vendored decadriver reads through extern declarations to
 * trace the RF-arm path. They are diagnostics, but they are referenced from
 * dw3000_device.c, so the port has to define them or the image does not link.
 */
volatile uint32_t g_dw_cyc_gpio;
volatile uint32_t g_dw_cyc_work;
volatile uint32_t g_dw_cyc_isrdone;
volatile uint32_t g_dw_cyc_per_us = 64u; /* nRF52833 core clock, MHz. */

uint32_t dw3000_dwt_cyccnt(void)
{
	return woz_freertos_cycle_get_32();
}

static StaticTask_t s_task_tcb;
static StackType_t s_task_stack[WOZ_DW3000_ISR_TASK_STACK / sizeof(StackType_t)];
static TaskHandle_t s_task;

static bool s_asleep;
static bool s_irq_enabled;

void dw3000_hw_mark_asleep(void)
{
	s_asleep = true;
}

bool dw3000_hw_is_asleep(void)
{
	return s_asleep;
}

int dw3000_hw_init(void)
{
	/*
	 * Reset released: an input, held high by the module's pull-up. Driving
	 * it high instead would fight the chip whenever it resets itself.
	 */
	nrf_gpio_cfg_input(WOZ_DW3000_PIN_RST, NRF_GPIO_PIN_NOPULL);

	/* Wake line held asserted, matching the other two ports' idle state. */
	nrf_gpio_pin_set(WOZ_DW3000_PIN_WAKEUP);
	nrf_gpio_cfg_output(WOZ_DW3000_PIN_WAKEUP);

	return dw3000_spi_init();
}

void woz_freertos_dw3000_irq_handler(void)
{
	BaseType_t wake = pdFALSE;

	if (!nrf_gpiote_event_check(NRF_GPIOTE, NRF_GPIOTE_EVENT_IN_0)) {
		return;
	}
	nrf_gpiote_event_clear(NRF_GPIOTE, NRF_GPIOTE_EVENT_IN_0);

	g_dw_cyc_gpio = woz_freertos_cycle_get_32();
	if (s_task != NULL) {
		vTaskNotifyGiveFromISR(s_task, &wake);
		portYIELD_FROM_ISR(wake);
	}
}

static void dw3000_isr_task(void *arg)
{
	(void)arg;

	for (;;) {
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		g_dw_cyc_work = woz_freertos_cycle_get_32();
		/*
		 * The line stays high until every pending event has been read
		 * out, so one notification can owe several passes. Draining it
		 * here rather than returning to the vector keeps the whole
		 * frame-pull on one task at one priority.
		 */
		while (nrf_gpio_pin_read(WOZ_DW3000_PIN_IRQ)) {
			dwt_isr();
		}
		g_dw_cyc_isrdone = woz_freertos_cycle_get_32();
	}
}

int dw3000_hw_init_interrupt(void)
{
	nrf_gpio_cfg_input(WOZ_DW3000_PIN_IRQ, NRF_GPIO_PIN_NOPULL);

	if (s_task == NULL) {
		s_task = xTaskCreateStatic(dw3000_isr_task, "dw3000_isr",
					   sizeof(s_task_stack) / sizeof(StackType_t), NULL,
					   WOZ_DW3000_ISR_TASK_PRIORITY, s_task_stack,
					   &s_task_tcb);
		if (s_task == NULL) {
			woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, "no worker task");
			return -1;
		}
	}

	/*
	 * The vector belongs to board/gpiote_freertos.c, because the update
	 * button takes a second channel on the same peripheral and a vector
	 * cannot have two definitions. Registered before the event is enabled:
	 * an edge that arrived with no handler installed would be cleared by
	 * nobody and would re-enter the vector forever.
	 */
	if (woz_freertos_gpiote_add_handler(woz_freertos_dw3000_irq_handler) != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, "no GPIOTE handler slot");
		return -1;
	}

	/*
	 * Rising edge, because the DW3110 asserts the line and holds it. The
	 * event is configured before it is enabled and cleared before the
	 * interrupt is unmasked, so a level left over from before this call
	 * cannot deliver a notification to a task that has not run yet.
	 */
	nrf_gpiote_event_configure(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL, WOZ_DW3000_PIN_IRQ,
				   NRF_GPIOTE_POLARITY_LOTOHI);
	nrf_gpiote_event_enable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL);
	nrf_gpiote_event_clear(NRF_GPIOTE, NRF_GPIOTE_EVENT_IN_0);

	NVIC_SetPriority(GPIOTE_IRQn, WOZ_DW3000_IRQ_PRIORITY);
	NVIC_ClearPendingIRQ(GPIOTE_IRQn);
	NVIC_EnableIRQ(GPIOTE_IRQn);

	nrf_gpiote_int_enable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
	s_irq_enabled = true;
	return 0;
}

/*
 * The decadriver's decamutexon and decamutexoff land here. They mask the
 * DW3110's own line and nothing else: the radio, the RTOS tick and MPSL all
 * keep running, which is the point -- this is a driver-level critical section,
 * not a global one.
 */
void dw3000_hw_interrupt_enable(void)
{
	if (!s_irq_enabled) {
		nrf_gpiote_int_enable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
		s_irq_enabled = true;
	}
}

void dw3000_hw_interrupt_disable(void)
{
	if (s_irq_enabled) {
		nrf_gpiote_int_disable(NRF_GPIOTE, NRF_GPIOTE_INT_IN0_MASK);
		s_irq_enabled = false;
	}
}

bool dw3000_hw_interrupt_is_enabled(void)
{
	return s_irq_enabled;
}

void dw3000_hw_reset(void)
{
	/*
	 * Assert by driving low, release by going back to an input. The line is
	 * open drain with the module's pull-up on it, so releasing means letting
	 * go rather than driving high.
	 */
	nrf_gpio_pin_clear(WOZ_DW3000_PIN_RST);
	nrf_gpio_cfg_output(WOZ_DW3000_PIN_RST);
	woz_freertos_busy_wait_us(1000);
	nrf_gpio_cfg_input(WOZ_DW3000_PIN_RST, NRF_GPIO_PIN_NOPULL);

	/* Long enough for the chip to climb from INIT_RC to IDLE_RC. */
	woz_freertos_busy_wait_us(2000);
	s_asleep = false;
}

void dw3000_hw_wakeup(void)
{
	unsigned spins = 0;

	if (!s_asleep) {
		return;
	}
	dw3000_spi_wakeup();
	woz_freertos_busy_wait_us(2000); /* INIT_RC to IDLE_RC. */
	s_asleep = false;

	/*
	 * Confirm the chip actually came back rather than assuming it did. A
	 * DW3110 still in INIT_RC answers SPI and returns nonsense, so the
	 * failure without this check is a ranging session that produces
	 * plausible numbers from a radio that was never configured.
	 */
	while (!dwt_checkidlerc() && spins < 500u) {
		woz_freertos_busy_wait_us(10);
		spins++;
	}
	if (spins >= 500u) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, "wake: never reached IDLE_RC");
	}
}

void dw3000_hw_wakeup_pin_low(void)
{
	nrf_gpio_pin_clear(WOZ_DW3000_PIN_WAKEUP);
}

void dw3000_hw_fini(void)
{
	dw3000_hw_interrupt_disable();
	nrf_gpiote_event_disable(NRF_GPIOTE, WOZ_DW3000_GPIOTE_CHANNEL);
	dw3000_spi_fini();
}
