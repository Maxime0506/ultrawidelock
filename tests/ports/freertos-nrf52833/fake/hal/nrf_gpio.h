/*
 * Register-level model of the nRF52833 GPIO, matching the pinned hal/nrf_gpio.h
 * surface the port uses.
 *
 * Direction, input-buffer connection, pull and level are all kept, because the
 * SPIM model reads them: a clock pin whose input buffer is disconnected is a
 * bus that never transfers, and that is invisible anywhere but on hardware.
 * Writing a pin that was never configured is recorded rather than ignored --
 * on the part it does nothing, which is exactly how a wrong pin number hides.
 */
#ifndef TEST_HAL_NRF_GPIO_H
#define TEST_HAL_NRF_GPIO_H

#include <stdbool.h>
#include <stdint.h>

/* The flat numbering's full span: two ports of 32, whether or not they exist. */
#define FAKE_GPIO_PIN_COUNT 64u

/*
 * How many of those the nRF52833 actually has: P0.00-P0.31 and P1.00-P1.09.
 * nrf52833.dtsi gives gpio1 ngpios = <10>, and the part's own
 * nrf_gpio_pin_port_decode() asserts on anything above.
 *
 * Modelled because the absence of this check let a pin that does not exist
 * pass the whole host suite. WOZ_DW3000_PIN_WAKEUP was 51 -- P1.19, copied
 * from a Qorvo project cmake that no source file in their SDK reads -- and
 * dw3000_hw_init() drove it on its first line. Every test agreed, because the
 * fake sized its array by the numbering rather than by the silicon, and the
 * board took an nrfx assertion at UWB start.
 */
#define FAKE_GPIO_PRESENT_COUNT 42u

typedef enum {
	NRF_GPIO_PIN_DIR_INPUT = 0,
	NRF_GPIO_PIN_DIR_OUTPUT = 1,
} nrf_gpio_pin_dir_t;

typedef enum {
	NRF_GPIO_PIN_INPUT_CONNECT = 0,
	NRF_GPIO_PIN_INPUT_DISCONNECT = 1,
} nrf_gpio_pin_input_t;

typedef enum {
	NRF_GPIO_PIN_NOPULL = 0,
	NRF_GPIO_PIN_PULLDOWN = 1,
	NRF_GPIO_PIN_PULLUP = 3,
} nrf_gpio_pin_pull_t;

typedef enum {
	NRF_GPIO_PIN_S0S1 = 0,
	NRF_GPIO_PIN_H0S1 = 1,
	NRF_GPIO_PIN_S0H1 = 2,
} nrf_gpio_pin_drive_t;

typedef enum {
	NRF_GPIO_PIN_NOSENSE = 0,
	NRF_GPIO_PIN_SENSE_LOW = 3,
	NRF_GPIO_PIN_SENSE_HIGH = 2,
} nrf_gpio_pin_sense_t;

typedef struct {
	bool configured;
	nrf_gpio_pin_dir_t dir;
	nrf_gpio_pin_input_t input;
	nrf_gpio_pin_pull_t pull;
	nrf_gpio_pin_drive_t drive;
	nrf_gpio_pin_sense_t sense;
	bool level;
	/* Level changes, so a test can count a pulse rather than sample it. */
	unsigned writes;
} fake_gpio_pin_t;

extern fake_gpio_pin_t fake_gpio[FAKE_GPIO_PIN_COUNT];
/* Writes to pins that were never configured as outputs. */
extern unsigned fake_gpio_unconfigured_writes;
/*
 * Any touch -- configure, write or read -- of a pin the part does not have.
 * On hardware the first of these is a fatal nrfx assertion, so a test that
 * leaves this non-zero is describing a board that does not boot.
 */
extern unsigned fake_gpio_absent_pin_touches;

void nrf_gpio_cfg(uint32_t pin, nrf_gpio_pin_dir_t dir, nrf_gpio_pin_input_t input,
		  nrf_gpio_pin_pull_t pull, nrf_gpio_pin_drive_t drive, nrf_gpio_pin_sense_t sense);
void nrf_gpio_cfg_output(uint32_t pin);
void nrf_gpio_cfg_input(uint32_t pin, nrf_gpio_pin_pull_t pull);
void nrf_gpio_cfg_default(uint32_t pin);
void nrf_gpio_pin_set(uint32_t pin);
void nrf_gpio_pin_clear(uint32_t pin);
void nrf_gpio_pin_write(uint32_t pin, uint32_t value);
uint32_t nrf_gpio_pin_read(uint32_t pin);
uint32_t nrf_gpio_pin_out_read(uint32_t pin);

void fake_gpio_reset(void);
/* Drive a pin from outside, standing for whatever the DW3110 does to it. */
void fake_gpio_input_set(uint32_t pin, bool level);

#endif /* TEST_HAL_NRF_GPIO_H */
