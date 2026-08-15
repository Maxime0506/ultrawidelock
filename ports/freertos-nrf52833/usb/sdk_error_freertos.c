/* SPDX-License-Identifier: ISC */

/*
 * Where an nRF5 SDK error check ends up.
 *
 * APP_ERROR_CHECK is the SDK's assertion, and app_usbd uses it for the failures
 * it considers impossible: a class whose endpoints do not fit, a transfer
 * started on an endpoint that is not enabled. The SDK's own implementation
 * lives in app_error.c beside a logging framework and a fault handler this
 * image does not link, so the one symbol it leaves is defined here instead.
 *
 * FATAL, not logged and continued. Every caller of this is a place the vendor
 * code has decided it cannot proceed, and the alternative -- returning into a
 * USB stack that has just told us its state is inconsistent -- produces a board
 * that enumerates and then behaves arbitrarily. ultrawidelock_freertos_fatal() is the
 * port's one answer to that question and it is the same one the rest of the
 * image gives.
 *
 * The code is logged before the halt because it is the only thing that
 * distinguishes one of these from another: the SDK passes no file or line
 * through the bare variant.
 */
#include <stdint.h>

#include "ultrawidelock_freertos_platform.h"

void app_error_handler_bare(uint32_t error_code);

void app_error_handler_bare(uint32_t error_code)
{
	ultrawidelock_freertos_log(ULTRAWIDELOCK_FREERTOS_LOG_ERROR, "usb_sdk", "SDK error check failed, code %u",
			 (unsigned)error_code);
	ultrawidelock_freertos_fatal("nRF5 SDK error check");
}
