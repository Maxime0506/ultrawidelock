/* SPDX-License-Identifier: ISC */

// Lock-state indicator LED: maps lock state (and credential activity) to an RGB colour for the
// single status pixel. Locked always extinguishes the indicator; unlocked shows blue during active
// UWB/credential engagement and a different colour otherwise, per lock_led_color.
#include "lock_led.h"

struct lock_led_rgb lock_led_color(bool locked, bool ultrawidelock)
{
	struct lock_led_rgb c = { 0, 0, 0 };

	if (locked) {
		return c;
	}
	if (ultrawidelock) {
		c.b = LOCK_LED_BRIGHTNESS;
	} else {
		c.g = LOCK_LED_BRIGHTNESS;
	}
	return c;
}
