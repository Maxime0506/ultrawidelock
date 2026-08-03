/* nfcfake: <zephyr/drivers/gpio.h>. Recorder plus knobs; the callback
 * registration is real enough that a suite can fire the IRQ handler the way an
 * edge would. */
#ifndef NFCFAKE_ZEPHYR_DRIVERS_GPIO_H
#define NFCFAKE_ZEPHYR_DRIVERS_GPIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

typedef uint32_t gpio_port_pins_t;
typedef uint32_t gpio_flags_t;
typedef uint8_t gpio_pin_t;
typedef uint32_t gpio_dt_flags_t;

#define GPIO_INPUT           (1u << 8)
#define GPIO_OUTPUT          (1u << 9)
#define GPIO_OUTPUT_INACTIVE (GPIO_OUTPUT | (1u << 10))
#define GPIO_INT_EDGE_TO_ACTIVE (1u << 16)

struct gpio_dt_spec {
	const struct device *port;
	gpio_pin_t pin;
	gpio_dt_flags_t dt_flags;
};

struct gpio_callback;
typedef void (*gpio_callback_handler_t)(const struct device *port, struct gpio_callback *cb,
					gpio_port_pins_t pins);

struct gpio_callback {
	gpio_callback_handler_t handler;
	gpio_port_pins_t pin_mask;
};

/* The overlay wires irq-gpios, so the _OR default is not taken on the shipping
 * board. A suite reaches the polling path by clearing .port at runtime, which
 * it can do because both this struct and the driver's are ours here. */
#define GPIO_DT_SPEC_INST_GET_OR(inst, prop, default_value)                                        \
	{                                                                                          \
		.port = &nfcfake_gpio_port, .pin = 8, .dt_flags = 0                                 \
	}

/* C linkage: the driver is C and the fake that implements these is C++. */
#ifdef __cplusplus
extern "C" {
#endif

bool gpio_is_ready_dt(const struct gpio_dt_spec *spec);
int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t flags);
int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value);
void gpio_init_callback(struct gpio_callback *callback, gpio_callback_handler_t handler,
			gpio_port_pins_t pin_mask);
int gpio_add_callback(const struct device *port, struct gpio_callback *callback);
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NFCFAKE_ZEPHYR_DRIVERS_GPIO_H */
