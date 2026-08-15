/* SPDX-License-Identifier: ISC */

#ifndef ULTRAWIDELOCK_PIV_CCID_USB_H
#define ULTRAWIDELOCK_PIV_CCID_USB_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the ESP32-S3 native USB-OTG port as a single-slot CCID reader.
 *
 * This symbol is built only when CONFIG_ULTRAWIDELOCK_PIV_CCID is enabled.
 */
esp_err_t piv_ccid_usb_start(void);

#ifdef __cplusplus
}
#endif

#endif
