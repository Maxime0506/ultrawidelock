/* matterfake iot_button.h — the espressif/button v4 surface app_main uses to
 * hang the commissioning-window recovery off a long press (matterfake.cc).
 *
 * button_handle_t comes from bsp/esp-bsp.h, which the real esp-bsp also owns,
 * so the two fakes agree on one definition rather than racing to typedef it.
 */
#ifndef MATTERFAKE_IOT_BUTTON_H
#define MATTERFAKE_IOT_BUTTON_H

#include "bsp/esp-bsp.h"
#include "esp_err.h"

typedef enum {
	BUTTON_PRESS_DOWN = 0,
	BUTTON_PRESS_UP,
	BUTTON_SINGLE_CLICK,
	BUTTON_LONG_PRESS_START,
	BUTTON_LONG_PRESS_HOLD,
	BUTTON_LONG_PRESS_UP,
} button_event_t;

/* Opaque here: the production code passes NULL to take the Kconfig default
 * long-press time, which is the only way this fake is exercised. */
typedef struct button_event_args_t button_event_args_t;

typedef void (*button_cb_t)(void *button_handle, void *usr_data);

esp_err_t iot_button_register_cb(button_handle_t btn_handle, button_event_t event,
				 button_event_args_t *event_args, button_cb_t cb, void *usr_data);

#endif /* MATTERFAKE_IOT_BUTTON_H */
