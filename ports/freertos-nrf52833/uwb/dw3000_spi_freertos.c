/*
 * The DW3110 SPI link on SPIM3, for the standalone FreeRTOS port.
 *
 * This implements modules/ultrawidelock_dw3000's dw3000_spi.h, the same five entry points
 * the Zephyr and ESP-IDF backends implement. Nothing in modules/ changes to
 * accommodate it; a new target is a new file here and nothing else.
 *
 * WHY THE TRANSFER IS POLLED. The Zephyr oracle discovered that the per-slot
 * RX-arm path is overhead-bound rather than clock-bound: taking its SPI from
 * 16 to 32 MHz moved the dwt_isr frame-pull by zero cycles, because the cost was
 * the driver's per-transfer semaphore block and the two context switches that
 * came with it, paid several times per pull. It works around that by starting an
 * asynchronous transfer and spinning on the completion signal. There is no such
 * driver in the way here, so the same idea reaches its conclusion: the caller
 * spins on the peripheral's own END event, and the SPIM vector is never enabled
 * at all. An enabled vector would add an interrupt entry and exit per transfer,
 * several per frame-pull, to tell a task something it is already watching.
 *
 * WHY THERE IS A BOUNCE BUFFER. EasyDMA reads and writes RAM only; a pointer
 * into flash is not slow, it is a fault. The decadriver hands us header buffers
 * from the stack but bodies that are often const and therefore in flash, so
 * every transfer is assembled into a static RAM buffer first. That also lets one
 * command with a header, a body and a CRC byte go out as a single CS-low
 * transaction, which is what the DW3110 expects.
 *
 * WHY CS IS A GPIO. Waking the chip means holding CS low for 500 us with no
 * clock on the bus, and SPIM's own CSN only asserts around a transfer. So the
 * port drives CS itself, exactly as the other two backends do.
 *
 * Not safe from interrupt context, and not meant to be: it takes a mutex,
 * because dwt_isr runs on a task here and the application configures the radio
 * from another one. The decadriver's own decamutexon only masks the DW3110 IRQ
 * line, which says nothing about a second task.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_spim.h>
#include <nrfx.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <woz_freertos_platform.h>

#include "board_pins.h"
#include "dw3000_spi.h"

#define TAG "dw3000_spi"

#define DW_XFER_MAX ULTRAWIDELOCK_DW3000_SPI_XFER_MAX

/*
 * How long a transfer may take before the port gives up on it. At the slow
 * clock, 1028 bytes are about 4.1 ms of bit time, so a real transfer completes
 * in far fewer spins than this; the bound exists so a peripheral that never
 * raises END fails loudly instead of wedging the ranging task forever.
 */
#ifndef DW_XFER_SPIN_LIMIT
#define DW_XFER_SPIN_LIMIT 4000000u
#endif

static uint8_t s_tx[DW_XFER_MAX] __attribute__((aligned(4)));
static uint8_t s_rx[DW_XFER_MAX] __attribute__((aligned(4)));

static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static nrf_spim_frequency_t s_freq = NRF_SPIM_FREQ_2M;

static nrf_spim_frequency_t freq_of(uint32_t hz)
{
	if (hz >= 32000000u) {
		return NRF_SPIM_FREQ_32M;
	}
	if (hz >= 16000000u) {
		return NRF_SPIM_FREQ_16M;
	}
	if (hz >= 8000000u) {
		return NRF_SPIM_FREQ_8M;
	}
	if (hz >= 4000000u) {
		return NRF_SPIM_FREQ_4M;
	}
	return NRF_SPIM_FREQ_2M;
}

/* CS is active low and idles high. */
static void cs_assert(void)
{
	nrf_gpio_pin_clear(ULTRAWIDELOCK_DW3000_PIN_CS);
}

static void cs_release(void)
{
	nrf_gpio_pin_set(ULTRAWIDELOCK_DW3000_PIN_CS);
}

int dw3000_spi_init(void)
{
	if (s_ready) {
		return 0;
	}

	if (s_lock == NULL) {
		s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
		if (s_lock == NULL) {
			return -1;
		}
	}

	/* CS first and idle high, so enabling the bus cannot strobe the chip. */
	nrf_gpio_pin_set(ULTRAWIDELOCK_DW3000_PIN_CS);
	nrf_gpio_cfg_output(ULTRAWIDELOCK_DW3000_PIN_CS);

	/*
	 * SCK carries the clock's idle level before the peripheral drives it,
	 * which for SPI mode 0 is low. Its input buffer has to stay connected:
	 * the SPIM samples its own clock line through it, and a pin configured
	 * as a plain output produces a bus that never transfers anything.
	 */
	nrf_gpio_pin_clear(ULTRAWIDELOCK_DW3000_PIN_SCLK);
	nrf_gpio_cfg(ULTRAWIDELOCK_DW3000_PIN_SCLK, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);

	nrf_gpio_pin_clear(ULTRAWIDELOCK_DW3000_PIN_MOSI);
	nrf_gpio_cfg_output(ULTRAWIDELOCK_DW3000_PIN_MOSI);

	/* Pulled down, matching the Qorvo baseline, so a floating MISO reads 0. */
	nrf_gpio_cfg_input(ULTRAWIDELOCK_DW3000_PIN_MISO, NRF_GPIO_PIN_PULLDOWN);

	nrf_spim_pins_set(NRF_SPIM3, ULTRAWIDELOCK_DW3000_PIN_SCLK, ULTRAWIDELOCK_DW3000_PIN_MOSI,
			  ULTRAWIDELOCK_DW3000_PIN_MISO);
	nrf_spim_configure(NRF_SPIM3, NRF_SPIM_MODE_0, NRF_SPIM_BIT_ORDER_MSB_FIRST);
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_SLOW_HZ);
	nrf_spim_frequency_set(NRF_SPIM3, s_freq);

	/*
	 * Bytes clocked out past the end of the TX buffer. Zero rather than the
	 * reset value of 0x00 by accident: a read command clocks its response
	 * out of a buffer we filled with zeros, and the DW3110 must see idle
	 * MOSI while it answers.
	 */
	nrf_spim_orc_set(NRF_SPIM3, 0);

	nrf_spim_enable(NRF_SPIM3);
	s_ready = true;
	return 0;
}

void dw3000_spi_speed_slow(void)
{
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_SLOW_HZ);
	nrf_spim_frequency_set(NRF_SPIM3, s_freq);
}

void dw3000_spi_speed_fast(void)
{
	s_freq = freq_of(ULTRAWIDELOCK_DW3000_SPI_FAST_HZ);
	nrf_spim_frequency_set(NRF_SPIM3, s_freq);
}

void dw3000_spi_fini(void)
{
	if (!s_ready) {
		return;
	}
	nrf_spim_disable(NRF_SPIM3);
	cs_release();
	s_ready = false;
}

/*
 * Abandon a transfer that never raised END.
 *
 * Just walking away is not an option: EasyDMA would still be writing into the
 * receive buffer, and the next transfer would be assembling its own bytes into
 * the same memory. STOP is requested and STOPPED waited for, bounded again so
 * that a peripheral wedged past all recovery still returns control.
 */
static void xfer_abort(void)
{
	uint32_t spins = 0;

	nrf_spim_event_clear(NRF_SPIM3, NRF_SPIM_EVENT_STOPPED);
	nrf_spim_task_trigger(NRF_SPIM3, NRF_SPIM_TASK_STOP);
	while (!nrf_spim_event_check(NRF_SPIM3, NRF_SPIM_EVENT_STOPPED) &&
	       ++spins < DW_XFER_SPIN_LIMIT) {
		/* Spin. */
	}
	nrf_spim_event_clear(NRF_SPIM3, NRF_SPIM_EVENT_STOPPED);
}

/*
 * One CS-low command: [header][body or zeros][crc], captured into s_rx when the
 * caller wants a response.
 *
 * Receive is requested for the whole transaction rather than just its tail,
 * because EasyDMA has one receive pointer and starts filling it with the first
 * byte on the wire. The response therefore lands at the same offset the request
 * occupied, and the body slice is copied out afterwards.
 */
static int32_t dw_xfer(const uint8_t *hdr, uint16_t hlen, const uint8_t *body, uint16_t blen,
		       uint8_t *rx_body, const uint8_t *crc)
{
	size_t total = (size_t)hlen + (size_t)blen + (crc != NULL ? 1u : 0u);
	uint32_t spins = 0;
	int32_t ret = 0;

	if (!s_ready) {
		return -1;
	}
	if (hdr == NULL || hlen == 0u || total > DW_XFER_MAX) {
		return -1;
	}

	(void)xSemaphoreTake(s_lock, portMAX_DELAY);

	memcpy(s_tx, hdr, hlen);
	if (body != NULL && blen != 0u) {
		memcpy(s_tx + hlen, body, blen);
	} else if (blen != 0u) {
		memset(s_tx + hlen, 0, blen);
	}
	if (crc != NULL) {
		s_tx[hlen + blen] = *crc;
	}

	nrf_spim_tx_buffer_set(NRF_SPIM3, s_tx, (size_t)total);
	nrf_spim_rx_buffer_set(NRF_SPIM3, s_rx, rx_body != NULL ? (size_t)total : 0u);

	/*
	 * The previous transfer leaves END set, and a stale one would end this
	 * wait before a single bit had moved -- the caller would then read the
	 * bytes of the command before last. Clearing it here, and only here, is
	 * the whole correctness of a polled driver: clearing it again after the
	 * wait would look tidier and would quietly make this line optional.
	 */
	nrf_spim_event_clear(NRF_SPIM3, NRF_SPIM_EVENT_END);

	cs_assert();
	nrf_spim_task_trigger(NRF_SPIM3, NRF_SPIM_TASK_START);
	while (!nrf_spim_event_check(NRF_SPIM3, NRF_SPIM_EVENT_END) &&
	       ++spins < DW_XFER_SPIN_LIMIT) {
		/* Spin. A transfer that has started ends in microseconds. */
	}
	if (!nrf_spim_event_check(NRF_SPIM3, NRF_SPIM_EVENT_END)) {
		xfer_abort();
		ret = -1;
	}
	cs_release();

	if (ret == 0 && rx_body != NULL && blen != 0u) {
		memcpy(rx_body, s_rx + hlen, blen);
	}

	(void)xSemaphoreGive(s_lock);

	if (ret != 0) {
		woz_freertos_log(WOZ_FREERTOS_LOG_ERROR, TAG, "transfer of %u bytes never ended",
				 (unsigned)total);
	}
	return ret;
}

int32_t dw3000_spi_read(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength,
			uint8_t *readBuffer)
{
	return dw_xfer(headerBuffer, headerLength, NULL, readLength, readBuffer, NULL);
}

int32_t dw3000_spi_write(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength,
			 const uint8_t *bodyBuffer)
{
	return dw_xfer(headerBuffer, headerLength, bodyBuffer, bodyLength, NULL, NULL);
}

int32_t dw3000_spi_write_crc(uint16_t headerLength, const uint8_t *headerBuffer,
			     uint16_t bodyLength, const uint8_t *bodyBuffer, uint8_t crc8)
{
	return dw_xfer(headerBuffer, headerLength, bodyBuffer, bodyLength, NULL, &crc8);
}

/*
 * The CS-toggle wake. The DW3110 leaves sleep when CS is held low for at least
 * 500 us with the bus otherwise idle, so this is a GPIO operation with no
 * transfer behind it -- which is why CS is not SPIM's own chip select.
 */
void dw3000_spi_wakeup(void)
{
	cs_assert();
	woz_freertos_busy_wait_us(500);
	cs_release();
}

void dw3000_spi_trace_output(void)
{
	/* SPI tracing is not built on this port. */
}
