/* SPDX-License-Identifier: ISC */

/*
 * The four board LEDs as one state display, and the only way to drive them.
 *
 * Every LED on this board goes through status_led_signal(). Nothing else may
 * touch them: two owners toggling the same pin from a timer and a loop produce
 * a light that flickers between two truths, which is worse than no light.
 *
 * A signal is a fact about the board, not a blink rate. Callers say what is
 * true; status_led.c decides which LED shows it and how. That split is what
 * lets the whole display be re-mapped in one function instead of in five call
 * sites, and it is why every caller is one line.
 *
 * THE SAME SIGNALS AND THE SAME PICTURE as the Zephyr image, deliberately: the
 * two run on the same board in front of the same person, and an LED that means
 * one thing on one image and another on the other is worse than no LED. See
 * apps/dwm3001cdk-lock/src/status_led.h for the other half.
 *
 * Safe from any task: the setter is one masked read-modify-write and a timer
 * reschedule, so it can be called from the BLE host task, from OpenThread, or
 * from the reader loop without taking a lock or blocking. NOT safe from an
 * interrupt, and it must never be called from the DW3110 callbacks -- nothing
 * may be, the arm deadline there is ~1836 us.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_STATUS_LED_H
#define ULTRAWIDELOCK_FREERTOS_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

/*
 * What is true about the board. Combine with OR; each bit is set and cleared
 * independently, so two callers owning two different bits cannot fight.
 *
 * The values match the Zephyr image's enum bit for bit. They are written as
 * shifts rather than BIT() because that macro is Zephyr's.
 */
enum status_led_signal {
	/** The lock is open: a credential grant, or a Home tile tap. */
	STATUS_LED_UNLOCKED = (1u << 0),
	/** An secure credential session is established -- a phone is talking to us. */
	STATUS_LED_SESSION = (1u << 1),
	/** UWB ranges are landing right now. Implies a walk-up in progress. */
	STATUS_LED_RANGING = (1u << 2),
	/** The firmware update window is open (SW2, Apple Home, or the bench). */
	STATUS_LED_DFU_WINDOW = (1u << 3),
	/** Provisioning mode: USB console up, radios down, and it never returns. */
	STATUS_LED_PROV_MODE = (1u << 4),
	/** No Matter fabric stored: the board needs commissioning to work. */
	STATUS_LED_UNCOMMISSIONED = (1u << 5),
	/** Something unrecoverable failed. Latched by convention. */
	STATUS_LED_FAULT = (1u << 6),
};

/*
 * Configure the lamps and start the display. Never fails the boot: a board
 * that will not blink must still unlock a door, so a lamp that refuses to
 * configure is dropped and the rest carry on.
 *
 * Call from a task. The tick runs on the OSAL's work queue, which does not
 * exist until the scheduler does.
 */
void status_led_start(void);

/*
 * Assert (@p on true) or clear @p signals, which may be several ORed together.
 *
 * Idempotent and cheap: a call that changes nothing costs one compare and
 * returns. A call that does change something restarts the blink phase, so a
 * transition lights within a millisecond rather than up to one slot later --
 * the quarter second of nothing after a button press is exactly the gap that
 * makes someone press again.
 */
void status_led_signal(uint32_t signals, bool on);

/*
 * Blink the lock LED six times over ~720 ms and return, for the one piece of
 * feedback that has to be given before the thing it confirms happens: the
 * factory-reset button, whose erase follows immediately.
 *
 * Blocking on purpose, and the only blocking call here.
 */
void status_led_boot_blink(void);

#endif /* ULTRAWIDELOCK_FREERTOS_STATUS_LED_H */
