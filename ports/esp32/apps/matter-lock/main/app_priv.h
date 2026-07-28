/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

typedef void *app_driver_handle_t;

/** Initialize the button driver
 *
 * This initializes the button driver associated with the selected board.
 *
 * @return Handle on success.
 * @return NULL in case of failure.
 */
app_driver_handle_t app_driver_button_init();

/** Initialize the onboard WS2812 used as the bolt-state indicator.
 *
 * @return ESP_OK on success, error otherwise (the indicator then stays dark).
 */
esp_err_t app_driver_led_init();

/** Open a basic Matter commissioning window.
 *
 * The recovery path for a device that is commissioned but unreachable: a
 * commissioned device does not advertise commissionable, and with no working
 * network no controller can reach it to open a window. Opening one here lets a
 * controller re-push Wi-Fi credentials over BLE through the NetworkCommissioning
 * cluster, keeping every fabric and the Aliro trust store. `factoryreset` is the
 * only other way out of that corner and it takes both.
 *
 * Basic rather than enhanced, so the QR and manual codes already printed at boot
 * still work. The open runs on the Matter task; failures are logged rather than
 * returned, because both callers are fire-and-forget.
 */
void app_commissioning_window_open();

/** Print the commissioning QR URL and manual pairing code to the console.
 *
 * Not PrintOnboardingCodes(): that logs at CHIP Progress level, which
 * CONFIG_LOG_DEFAULT_LEVEL_WARN compiles out of the CHIP library entirely, so it
 * emits nothing and no runtime log level brings it back. This printf's instead.
 */
void app_print_onboarding_codes();

/** Drive the bolt-state indicator.
 *
 * @param[in] locked true to extinguish the LED, false to light it.
 * @param[in] aliro true when the UWB approach path drove the unlock (lights
 *                  blue instead of green). Ignored when `locked` is true.
 */
void app_driver_led_lock_state(bool locked, bool aliro);

/** Driver Update
 *
 * This API should be called to update the driver for the attribute being updated.
 * This is usually called from the common `app_attribute_update_cb()`.
 *
 * @param[in] endpoint_id Endpoint ID of the attribute.
 * @param[in] cluster_id Cluster ID of the attribute.
 * @param[in] attribute_id Attribute ID of the attribute.
 * @param[in] val Pointer to `esp_matter_attr_val_t`. Use appropriate elements as per the value type.
 *
 * @return ESP_OK on success.
 * @return error in case of failure.
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

/** The definition and invocation of this function are intended to resolve the issue:
 * unable to link and call the callback functions defined in the door_lock_callbacks.cpp file.
*/
void door_lock_init();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
