/* SPDX-License-Identifier: ISC */

/**
 * @file
 * @brief The four board LEDs as one state display, and the only way to drive them.
 *
 * Every LED on this board goes through status_led_signal(). Nothing else may
 * touch led0..led3: two owners toggling the same pin from a work queue and a
 * loop produce a light that flickers between two truths, which is worse than no
 * light at all.
 *
 * A signal is a fact about the board, not a blink rate. Callers say what is
 * true; src/status_led.c decides which LED shows it and how. That split is what
 * lets the whole display be re-mapped in one function instead of in five call
 * sites, and it is why the callers below can be one line each.
 *
 * Safe from any task: the setter is one atomic store and a work submit, so it
 * can be called from the BLE host task, from OpenThread, or from the reader
 * loop between ranging rounds without taking a lock or blocking. It must never
 * be called from the DW3110 callbacks themselves -- nothing may be, the arm
 * deadline there is ~1836 us -- but the 250 ms reader loop is fine.
 */
#ifndef ULTRAWIDELOCK_STATUS_LED_H
#define ULTRAWIDELOCK_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/**
 * What is true about the board. Combine with OR; each bit is set and cleared
 * independently, so two callers owning two different bits cannot fight.
 */
enum status_led_signal {
	/** The lock is open: a credential grant, or a Home tile tap. */
	STATUS_LED_UNLOCKED = BIT(0),
	/** A credential secure session is established -- a phone is talking to us. */
	STATUS_LED_SESSION = BIT(1),
	/** UWB ranges are landing right now. Implies a walk-up in progress. */
	STATUS_LED_RANGING = BIT(2),
	/** The firmware update window is open (SW2, Apple Home, or the bench). */
	STATUS_LED_DFU_WINDOW = BIT(3),
	/** Provisioning mode: USB console up, radios down, main() never returns. */
	STATUS_LED_PROV_MODE = BIT(4),
	/** No Matter fabric stored: the board needs commissioning before it works. */
	STATUS_LED_UNCOMMISSIONED = BIT(5),
	/** Something that cannot be recovered at runtime failed. Latched by convention. */
	STATUS_LED_FAULT = BIT(6),
};

#if IS_ENABLED(CONFIG_ULTRAWIDELOCK_STATUS_LED)

/**
 * Assert (@p on true) or clear @p signals, which may be several ORed together.
 *
 * Idempotent and cheap: a call that changes nothing costs one compare and
 * returns. A call that does change something restarts the blink phase, so every
 * transition lights within a millisecond rather than up to one slot later --
 * the quarter second of nothing after a button press is exactly the gap that
 * makes someone press again.
 */
void status_led_signal(uint32_t signals, bool on);

/**
 * Blink the lock LED six times over ~720 ms and return, for the one piece of
 * feedback that has to be given before the thing it confirms happens: the
 * factory-reset button, whose erase follows immediately.
 *
 * Blocking on purpose, and the only blocking call here. It runs from main()
 * before the radios start, where 720 ms costs nothing and an asynchronous
 * signal would be overwritten by the erase's own state changes.
 */
void status_led_boot_blink(void);

#else /* !CONFIG_ULTRAWIDELOCK_STATUS_LED */

static inline void status_led_signal(uint32_t signals, bool on)
{
	ARG_UNUSED(signals);
	ARG_UNUSED(on);
}

static inline void status_led_boot_blink(void)
{
}

#endif /* CONFIG_ULTRAWIDELOCK_STATUS_LED */

#endif /* ULTRAWIDELOCK_STATUS_LED_H */
