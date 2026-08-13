/* Register-level nRF52833 GPIO model. See hal/nrf_gpio.h for what it enforces. */
#include "hal/nrf_gpio.h"

#include <string.h>

fake_gpio_pin_t fake_gpio[FAKE_GPIO_PIN_COUNT];
unsigned fake_gpio_unconfigured_writes;
unsigned fake_gpio_absent_pin_touches;

void fake_gpio_reset(void)
{
	memset(fake_gpio, 0, sizeof(fake_gpio));
	fake_gpio_unconfigured_writes = 0;
	fake_gpio_absent_pin_touches = 0;
}

/*
 * Whether the part has this pin, and the single place every entry point asks.
 *
 * The array is sized by the flat numbering so an out-of-range pin still has
 * somewhere to land and the model cannot be made to scribble; presence is a
 * separate question, and it is the one hardware answers with an assertion.
 */
static bool pin_present(uint32_t pin)
{
	if (pin < FAKE_GPIO_PRESENT_COUNT) {
		return true;
	}
	fake_gpio_absent_pin_touches++;
	return false;
}

void nrf_gpio_cfg(uint32_t pin, nrf_gpio_pin_dir_t dir, nrf_gpio_pin_input_t input,
		  nrf_gpio_pin_pull_t pull, nrf_gpio_pin_drive_t drive, nrf_gpio_pin_sense_t sense)
{
	if (!pin_present(pin)) {
		return;
	}
	fake_gpio[pin].configured = true;
	fake_gpio[pin].dir = dir;
	fake_gpio[pin].input = input;
	fake_gpio[pin].pull = pull;
	fake_gpio[pin].drive = drive;
	fake_gpio[pin].sense = sense;
}

void nrf_gpio_cfg_output(uint32_t pin)
{
	nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
		     NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
}

void nrf_gpio_cfg_input(uint32_t pin, nrf_gpio_pin_pull_t pull)
{
	nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, pull,
		     NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
}

void nrf_gpio_cfg_default(uint32_t pin)
{
	if (!pin_present(pin)) {
		return;
	}
	memset(&fake_gpio[pin], 0, sizeof(fake_gpio[pin]));
}

/*
 * OUT is a real register: the part latches a write whether or not the pin is an
 * output yet, and the level appears once the direction is set. Setting CS high
 * before configuring it as an output is the port relying on exactly that, so
 * the model keeps the level regardless of configuration.
 *
 * Only level changes are counted. A test that wants to see a pulse can then
 * count edges rather than sample a line it will always find back at rest.
 */
static void pin_write(uint32_t pin, bool level)
{
	if (!pin_present(pin)) {
		return;
	}
	if (!fake_gpio[pin].configured) {
		fake_gpio_unconfigured_writes++;
	}
	if (fake_gpio[pin].level != level) {
		fake_gpio[pin].writes++;
	}
	fake_gpio[pin].level = level;
}

void nrf_gpio_pin_set(uint32_t pin)
{
	pin_write(pin, true);
}

void nrf_gpio_pin_clear(uint32_t pin)
{
	pin_write(pin, false);
}

void nrf_gpio_pin_write(uint32_t pin, uint32_t value)
{
	pin_write(pin, value != 0u);
}

uint32_t nrf_gpio_pin_read(uint32_t pin)
{
	if (!pin_present(pin)) {
		return 0;
	}
	return fake_gpio[pin].level ? 1u : 0u;
}

uint32_t nrf_gpio_pin_out_read(uint32_t pin)
{
	return nrf_gpio_pin_read(pin);
}

void fake_gpio_input_set(uint32_t pin, bool level)
{
	if (!pin_present(pin)) {
		return;
	}
	fake_gpio[pin].level = level;
}
