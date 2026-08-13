/*
 * The nRF5 SDK configuration this image's USB stack is built with.
 *
 * The SDK reads every setting through this one header, and the file it ships
 * for each example is ~4,000 lines of every option the SDK has. This is the
 * subset the USB device stack and CDC ACM class actually read, with the reason
 * for each value, and nothing else -- the same treatment ble/nimble_syscfg
 * gives NimBLE's syscfg.
 *
 * Anything not defined here is not defaulted by the SDK: sdk_config.h is
 * included before the library headers, and each of them supplies its own
 * default with #ifndef. So an option's absence means "upstream's default",
 * which is the correct meaning and is why this file can be short.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_SDK_CONFIG_H
#define ULTRAWIDELOCK_FREERTOS_SDK_CONFIG_H

/* ---- the USB device driver (nrfx) ---------------------------------------- */

#define NRFX_USBD_ENABLED 1
#define USBD_ENABLED 1

/*
 * Priority 6, below everything with a deadline.
 *
 * USB is the one peripheral here with no real-time constraint: the host retries
 * a NAKed packet and the console is a human typing. Above the FreeRTOS syscall
 * ceiling of 4 it could not call the FromISR APIs the event queue needs, and
 * below MPSL it would delay the radio -- so it sits under both. peripherals.yml
 * records the claim.
 */
#define NRFX_USBD_CONFIG_IRQ_PRIORITY 6
#define USBD_CONFIG_IRQ_PRIORITY 6
/*
 * app_usbd asserts that these two agree when its event queue is enabled. This
 * value makes the assertion pass and IS NOT WHAT THE HARDWARE DOES: the POWER
 * events arrive on MPSL's vector at priority 0, because MPSL owns POWER_CLOCK.
 * usb/usb_power_freertos.c carries the argument for why the queue is still
 * correct across the two priorities, and it is not this macro.
 */
#define POWER_CONFIG_IRQ_PRIORITY 6

/*
 * DMA scheduler: round robin rather than the priority-fixed variant. With one
 * class and three endpoints there is nothing to prioritise, and the priority
 * scheduler can starve an endpoint that is never the highest.
 */
#define NRFX_USBD_CONFIG_DMASCHEDULER_MODE 0
#define USBD_CONFIG_DMASCHEDULER_MODE 0
#define NRFX_USBD_CONFIG_DMASCHEDULER_ISO_BOOST 1
#define USBD_CONFIG_DMASCHEDULER_ISO_BOOST 1
/* No isochronous endpoints in this image; the setting only affects how ISO
 * endpoints are reported, and reporting one that does not exist is worse. */
#define NRFX_USBD_CONFIG_ISO_IN_ZLP 0
#define USBD_CONFIG_ISO_IN_ZLP 0

/* ---- the app_usbd framework ---------------------------------------------- */

#define APP_USBD_ENABLED 1

/*
 * The VID is Nordic's and the PID is the SDK's CDC ACM example.
 *
 * Deliberately not invented. A made-up VID/PID is someone else's registered
 * pair, and on a Mac it makes this board collide in the USB device tree with
 * whatever really owns it. Nordic publishes this pair for exactly this use, the
 * host driver on every platform this console is used from binds CDC ACM by
 * class rather than by ID, and the port that carries it is a bench port that
 * only exists in provisioning mode.
 */
#define APP_USBD_VID 0x1915
#define APP_USBD_PID 0x521A

#define APP_USBD_DEVICE_VER_MAJOR 1
#define APP_USBD_DEVICE_VER_MINOR 0
#define APP_USBD_DEVICE_VER_SUB 0

/*
 * Self-powered, because it is: the DWM3001CDK runs from the debug port and this
 * second USB connector supplies no rail the board depends on. Reporting
 * bus-powered would tell the host it may cut power to save it.
 */
#define APP_USBD_CONFIG_SELF_POWERED 1
#define APP_USBD_CONFIG_MAX_POWER 100

/*
 * Events go through a queue and are processed by app_usbd_event_queue_process()
 * on a task, rather than being handled in the USBD interrupt.
 *
 * This is the setting that makes the stack usable from a FreeRTOS task at all.
 * The alternative runs class callbacks in the vector at priority 6, where the
 * console's own read/write path could not take a mutex or wake a task.
 */
#define APP_USBD_CONFIG_EVENT_QUEUE_ENABLE 1
#define APP_USBD_CONFIG_EVENT_QUEUE_SIZE 32

/*
 * Let the stack watch the POWER peripheral's USB events itself. What it
 * actually reaches is usb/usb_power_freertos.c, because POWER_CLOCK belongs to
 * MPSL on this board and the SDK's own power driver would fight it.
 */
#define APP_USBD_CONFIG_POWER_EVENTS_PROCESS 1

/*
 * SOF is a 1 kHz interrupt. The stack needs it internally for its transfer
 * timing, but nothing above wants to see one, so it is not handled by the
 * application and not delivered to classes.
 */
#define APP_USBD_CONFIG_SOF_HANDLING_MODE 1
#define APP_USBD_CONFIG_SOF_TIMESTAMP_PROVIDE 0

/*
 * String descriptors.
 *
 * The framework builds an enum of string IDs out of these, so each one needs
 * both an index and a value; the indices are 1..4 in the order the SDK's own
 * examples use, because nothing outside the descriptor table reads them.
 *
 * The serial number is the exception and is marked EXTERN: it cannot be a
 * literal, because it is derived from FICR at startup by
 * app_usbd_serial_num_generate(). That is what makes two boards plugged into
 * one bench distinguishable in the host's device list -- without it, both
 * enumerate identically and the operator has no way to tell which terminal
 * belongs to which lock.
 */
#define APP_USBD_STRINGS_LANGIDS                                                                   \
	APP_USBD_LANG_AND_SUBLANG(APP_USBD_LANG_ENGLISH, APP_USBD_SUBLANG_ENGLISH_US)

#define APP_USBD_STRING_ID_MANUFACTURER 1
#define APP_USBD_STRINGS_MANUFACTURER_EXTERN 0
#define APP_USBD_STRINGS_MANUFACTURER APP_USBD_STRING_DESC("UltraWideLock")

#define APP_USBD_STRING_ID_PRODUCT 2
#define APP_USBD_STRINGS_PRODUCT_EXTERN 0
#define APP_USBD_STRINGS_PRODUCT APP_USBD_STRING_DESC("Provisioning Console")

#define APP_USBD_STRING_ID_SERIAL 3
#define APP_USBD_STRING_SERIAL_EXTERN 1
#define APP_USBD_STRING_SERIAL g_extern_serial_number

#define APP_USBD_STRING_ID_CONFIGURATION 4
#define APP_USBD_STRING_CONFIGURATION_EXTERN 0
#define APP_USBD_STRINGS_CONFIGURATION APP_USBD_STRING_DESC("Default configuration")

/* No product-specific strings beyond the four above. The macro is expanded into
 * an X-list, so it has to exist even when it is empty. */
#define APP_USBD_STRINGS_USER

#define APP_USBD_CONFIG_DESC_STRING_SIZE 31
#define APP_USBD_CONFIG_DESC_STRING_UTF_ENABLED 0

/* The SDK's own logging, which this image does not link. Named explicitly
 * rather than left to a default because app_usbd reads it directly. */
#define APP_USBD_CONFIG_LOG_ENABLED 0
#define APP_USBD_CDC_ACM_CONFIG_LOG_ENABLED 0
#define NRFX_USBD_CONFIG_LOG_ENABLED 0

/* ---- CDC ACM -------------------------------------------------------------- */

#define APP_USBD_CDC_ACM_ENABLED 1
/* Zero-length packet handling on the OUT endpoint, which the host sends to end
 * a transfer whose length is a multiple of the packet size. Without it a
 * 64-byte paste from a terminal never completes. */
#define APP_USBD_CDC_ACM_ZLP_ON_EPSIZE_WRITE 1

/* ---- SDK libraries the above pull in -------------------------------------- */

#define NRF_ATFIFO_ENABLED 1
/*
 * The SDK's logging framework is not linked. At zero its own nrf_log.h compiles
 * every NRF_LOG_* call to nothing, so no shim is needed -- this port has one
 * logger and it is board/log_rtt_freertos.c.
 */
#define NRF_LOG_ENABLED 0

#endif /* ULTRAWIDELOCK_FREERTOS_SDK_CONFIG_H */
