/* SPDX-License-Identifier: ISC */

/*
 * The USB CDC ACM serial port, and the console on top of it.
 *
 * This exists for one purpose on this board: the provisioning console. The
 * DWM3001CDK's second USB connector is wired straight to the nRF52833, and it
 * is the only input path the part has -- the debug port belongs to the on-board
 * probe, and the log is RTT, which is output only.
 *
 * Never brought up on an operational boot. ultrawidelock_freertos_usb_start() is called
 * only in provisioning mode, so a lock in a hallway pays the flash for this and
 * never runs a USB interrupt.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_USB_H
#define ULTRAWIDELOCK_FREERTOS_USB_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Bring up the USB device stack and the CDC ACM port, and start the task that
 * pumps its event queue.
 *
 * Returns 0, or -1 if any stage failed. Returning 0 means the stack is
 * enumerating, not that a host has opened the port; ultrawidelock_freertos_usb_ready()
 * answers that.
 *
 * Requires the radio to be up, because the crystal this needs is MPSL's to
 * grant. That is not a layering accident -- see usb/usb_power_freertos.c.
 */
int ultrawidelock_freertos_usb_start(void);

/** True once a host has enumerated the device and opened the serial port. */
bool ultrawidelock_freertos_usb_ready(void);

/**
 * Read one line, blocking until the host sends a terminator or @p max is
 * reached.
 *
 * Line-oriented rather than byte-oriented because every consumer is: the
 * console reads commands, and a caller that wanted bytes would have to
 * reimplement the echo and the backspace handling that make a terminal usable.
 *
 * @param out   buffer for the line, NUL-terminated on success
 * @param max   size of @p out, including the terminator
 * @return the line's length in characters, or -1 if the port closed
 */
int ultrawidelock_freertos_usb_readline(char *out, size_t max);

/** Write @p len bytes, blocking until the stack has taken them. Returns 0 or -1. */
int ultrawidelock_freertos_usb_write(const char *data, size_t len);

/** Write a NUL-terminated string followed by CRLF. */
int ultrawidelock_freertos_usb_print(const char *line);

/**
 * Run the provisioning console until the board is reset.
 *
 * Does not return. The four commands are the ones the Zephyr image's
 * `ultrawidelock` shell has -- prov, import, export, erase -- because the operator
 * procedure and the exported blobs are shared between the two images.
 */
void ultrawidelock_freertos_prov_console_run(void);

#endif /* ULTRAWIDELOCK_FREERTOS_USB_H */
