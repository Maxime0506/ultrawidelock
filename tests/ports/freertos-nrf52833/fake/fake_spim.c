/* Register-level nRF52833 SPIM model. See hal/nrf_spim.h for what it enforces. */
#include "hal/nrf_spim.h"

#include <string.h>

#include "hal/nrf_gpio.h"

fake_spim_t fake_spim;
uint32_t fake_spim_cs_pin = NRF_SPIM_PIN_NOT_CONNECTED;
bool fake_spim_cs_held = true;

/* Ranges the test has declared to be flash, which EasyDMA cannot reach. */
#define FAKE_SPIM_FLASH_RANGES 4u
static struct {
	const uint8_t *base;
	size_t length;
} s_flash[FAKE_SPIM_FLASH_RANGES];
static unsigned s_flash_count;

/* What the slave puts on MISO for the next transfer. */
static uint8_t s_response[FAKE_SPIM_BUS_MAX];
static size_t s_response_len;

void fake_spim_reset(void)
{
	memset(&fake_spim, 0, sizeof(fake_spim));
	memset(s_flash, 0, sizeof(s_flash));
	memset(s_response, 0, sizeof(s_response));
	s_flash_count = 0;
	s_response_len = 0;
	fake_spim_cs_pin = NRF_SPIM_PIN_NOT_CONNECTED;
	fake_spim_cs_held = true;
	fake_spim.psel_sck = NRF_SPIM_PIN_NOT_CONNECTED;
	fake_spim.psel_mosi = NRF_SPIM_PIN_NOT_CONNECTED;
	fake_spim.psel_miso = NRF_SPIM_PIN_NOT_CONNECTED;
}

void fake_spim_mark_flash(const void *ptr, size_t length)
{
	if (s_flash_count >= FAKE_SPIM_FLASH_RANGES) {
		return;
	}
	s_flash[s_flash_count].base = ptr;
	s_flash[s_flash_count].length = length;
	s_flash_count++;
}

void fake_spim_respond(const uint8_t *bytes, size_t length)
{
	if (length > sizeof(s_response)) {
		length = sizeof(s_response);
	}
	memcpy(s_response, bytes, length);
	s_response_len = length;
}

unsigned fake_spim_violations(void)
{
	return fake_spim.violations_ram + fake_spim.violations_disabled +
	       fake_spim.violations_stale_end + fake_spim.violations_sck +
	       fake_spim.violations_pins + fake_spim.violations_overlap +
	       fake_spim.violations_maxcnt;
}

static bool in_flash(const uint8_t *ptr, size_t length)
{
	unsigned i;

	for (i = 0; i < s_flash_count; i++) {
		const uint8_t *base = s_flash[i].base;

		if (ptr < base + s_flash[i].length && base < ptr + length) {
			return true;
		}
	}
	return false;
}

void nrf_spim_enable(NRF_SPIM_Type *p_reg)
{
	p_reg->enabled = true;
}

void nrf_spim_disable(NRF_SPIM_Type *p_reg)
{
	p_reg->enabled = false;
}

void nrf_spim_pins_set(NRF_SPIM_Type *p_reg, uint32_t sck, uint32_t mosi, uint32_t miso)
{
	p_reg->psel_sck = sck;
	p_reg->psel_mosi = mosi;
	p_reg->psel_miso = miso;
}

void nrf_spim_frequency_set(NRF_SPIM_Type *p_reg, nrf_spim_frequency_t frequency)
{
	p_reg->frequency = frequency;
}

void nrf_spim_configure(NRF_SPIM_Type *p_reg, nrf_spim_mode_t mode, nrf_spim_bit_order_t order)
{
	p_reg->mode = mode;
	p_reg->bit_order = order;
}

void nrf_spim_orc_set(NRF_SPIM_Type *p_reg, uint8_t orc)
{
	p_reg->orc = orc;
}

void nrf_spim_tx_buffer_set(NRF_SPIM_Type *p_reg, const uint8_t *buffer, size_t length)
{
	p_reg->tx_ptr = buffer;
	p_reg->tx_maxcnt = length;
}

void nrf_spim_rx_buffer_set(NRF_SPIM_Type *p_reg, uint8_t *buffer, size_t length)
{
	p_reg->rx_ptr = buffer;
	p_reg->rx_maxcnt = length;
}

/*
 * Everything TASKS_START has to be true for. A refused start leaves END clear,
 * which is what the peripheral does and what leaves a polled driver spinning
 * until its own bound saves it.
 */
static bool start_is_legal(NRF_SPIM_Type *p_reg)
{
	bool legal = true;

	if (!p_reg->enabled) {
		p_reg->violations_disabled++;
		legal = false;
	}
	if (p_reg->event_end) {
		p_reg->violations_stale_end++;
		legal = false;
	}
	if (p_reg->psel_sck == NRF_SPIM_PIN_NOT_CONNECTED ||
	    p_reg->psel_mosi == NRF_SPIM_PIN_NOT_CONNECTED ||
	    p_reg->psel_miso == NRF_SPIM_PIN_NOT_CONNECTED) {
		p_reg->violations_pins++;
		legal = false;
	} else if (p_reg->psel_sck < FAKE_GPIO_PIN_COUNT &&
		   (fake_gpio[p_reg->psel_sck].input != NRF_GPIO_PIN_INPUT_CONNECT ||
		    fake_gpio[p_reg->psel_sck].dir != NRF_GPIO_PIN_DIR_OUTPUT)) {
		/*
		 * The SPIM samples its own clock through SCK's input buffer.
		 * Disconnected, or left an input, the bus is silent -- and on
		 * hardware that failure looks like a dead chip, not a bad line
		 * of configuration.
		 */
		p_reg->violations_sck++;
		legal = false;
	}
	if (p_reg->tx_maxcnt > 0xFFFFu || p_reg->rx_maxcnt > 0xFFFFu) {
		p_reg->violations_maxcnt++;
		legal = false;
	}
	if (p_reg->tx_maxcnt > 0u && in_flash(p_reg->tx_ptr, p_reg->tx_maxcnt)) {
		p_reg->violations_ram++;
		legal = false;
	}
	if (p_reg->rx_maxcnt > 0u && in_flash(p_reg->rx_ptr, p_reg->rx_maxcnt)) {
		p_reg->violations_ram++;
		legal = false;
	}
	if (p_reg->tx_maxcnt > 0u && p_reg->rx_maxcnt > 0u &&
	    p_reg->tx_ptr < p_reg->rx_ptr + p_reg->rx_maxcnt &&
	    p_reg->rx_ptr < p_reg->tx_ptr + p_reg->tx_maxcnt) {
		/* Read and written concurrently: one buffer corrupts the send. */
		p_reg->violations_overlap++;
		legal = false;
	}
	return legal;
}

static void run_transfer(NRF_SPIM_Type *p_reg)
{
	size_t len = p_reg->tx_maxcnt > p_reg->rx_maxcnt ? p_reg->tx_maxcnt : p_reg->rx_maxcnt;
	size_t i;

	if (fake_spim_cs_pin != NRF_SPIM_PIN_NOT_CONNECTED && nrf_gpio_pin_read(fake_spim_cs_pin)) {
		/* Clocking with chip select released: the chip ignores every bit. */
		fake_spim_cs_held = false;
	}

	for (i = 0; i < len; i++) {
		/* MOSI: the TX buffer, then ORC once it runs out. */
		uint8_t out = i < p_reg->tx_maxcnt ? p_reg->tx_ptr[i] : p_reg->orc;

		if (p_reg->mosi_len < FAKE_SPIM_BUS_MAX) {
			p_reg->mosi[p_reg->mosi_len++] = out;
		}
		/* MISO: the programmed response, then zeros. */
		if (i < p_reg->rx_maxcnt) {
			p_reg->rx_ptr[i] = i < s_response_len ? s_response[i] : 0u;
		}
	}

	p_reg->transfers++;
	p_reg->last_len = len;
	p_reg->event_end = true;
}

void nrf_spim_task_trigger(NRF_SPIM_Type *p_reg, nrf_spim_task_t task)
{
	switch (task) {
	case NRF_SPIM_TASK_START:
		if (!start_is_legal(p_reg)) {
			return;
		}
		if (p_reg->stall) {
			/* Started and never finished: END stays clear. */
			return;
		}
		run_transfer(p_reg);
		break;
	case NRF_SPIM_TASK_STOP:
		p_reg->stops++;
		p_reg->event_stopped = true;
		break;
	default:
		break;
	}
}

void nrf_spim_event_clear(NRF_SPIM_Type *p_reg, nrf_spim_event_t event)
{
	switch (event) {
	case NRF_SPIM_EVENT_END:
		p_reg->event_end = false;
		break;
	case NRF_SPIM_EVENT_STOPPED:
		p_reg->event_stopped = false;
		break;
	default:
		break;
	}
}

bool nrf_spim_event_check(NRF_SPIM_Type *p_reg, nrf_spim_event_t event)
{
	switch (event) {
	case NRF_SPIM_EVENT_END:
		return p_reg->event_end;
	case NRF_SPIM_EVENT_STOPPED:
		return p_reg->event_stopped;
	default:
		return false;
	}
}
