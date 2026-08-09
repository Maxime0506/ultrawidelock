/* sdkfake driver/gpio.h — recording GPIO registers (fake_driver.c). */
#ifndef SDKFAKE_DRIVER_GPIO_H
#define SDKFAKE_DRIVER_GPIO_H

#include "../esp_err.h"

typedef enum {
	GPIO_MODE_DISABLE = 0,
	GPIO_MODE_INPUT,
	GPIO_MODE_OUTPUT,
} gpio_mode_t;

typedef enum {
	GPIO_INTR_DISABLE = 0,
	GPIO_INTR_POSEDGE,
} gpio_int_type_t;

typedef struct {
	uint64_t pin_bit_mask;
	gpio_mode_t mode;
	gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_level(int pin, uint32_t level);
int gpio_get_level(int pin);
esp_err_t gpio_set_direction(int pin, gpio_mode_t mode);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin, void (*isr)(void *), void *arg);
esp_err_t gpio_intr_enable(int pin);
esp_err_t gpio_intr_disable(int pin);

/* ---- fake_driver.c control surface (GPIO side) ---- */
#define FAKE_GPIO_MAX 64
extern gpio_mode_t fake_gpio_mode[FAKE_GPIO_MAX];
extern gpio_int_type_t fake_gpio_intr[FAKE_GPIO_MAX];
extern uint32_t fake_gpio_level[FAKE_GPIO_MAX];       /* last driven level */
extern uint32_t fake_gpio_input_level[FAKE_GPIO_MAX]; /* what gpio_get_level reads */
extern int fake_gpio_intr_enabled[FAKE_GPIO_MAX];
extern esp_err_t fake_gpio_isr_service_rc;
extern int fake_gpio_isr_service_installs;
extern void (*fake_gpio_isr[FAKE_GPIO_MAX])(void *);
extern void *fake_gpio_isr_arg[FAKE_GPIO_MAX];
/* Optional: called after every gpio_get_level (e.g. drop the IRQ line). */
extern void (*fake_gpio_get_level_hook)(int pin);

#endif
