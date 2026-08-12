/*
 * Host test for the bolt-state indicator policy (lock_led.c): which colour
 * the WS2812 shows for each lock state. The driver half (led_strip/RMT on
 * board-specific GPIO selection in app_driver.cpp) is target-only and not linked here.
 *
 * The case worth pinning is the channel assignment. led_strip_set_pixel()
 * takes (red, green, blue) while the WS2812 itself is GRB, so a swapped
 * green/blue is both easy to write and invisible to review: it still
 * lights, just the wrong colour for the wrong unlock path.
 */
#include <stdio.h>

#include "lock_led.h"

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

static int is_dark(struct lock_led_rgb c)
{
	return c.r == 0 && c.g == 0 && c.b == 0;
}

int main(void)
{
	struct lock_led_rgb locked, locked_ultrawidelock, remote, ultrawidelock;

	locked = lock_led_color(true, false);
	locked_ultrawidelock = lock_led_color(true, true);
	remote = lock_led_color(false, false);
	ultrawidelock = lock_led_color(false, true);

	printf("== locked extinguishes ==\n");
	okc("locked.dark", is_dark(locked));
	/* ultrawidelock is documented as ignored while locked: a stale source flag must
	 * never leave the indicator lit after a lock. */
	okc("locked.dark_ignores_ultrawidelock", is_dark(locked_ultrawidelock));

	printf("\n== unlocked lights ==\n");
	okc("remote.lit", !is_dark(remote));
	okc("ultrawidelock.lit", !is_dark(ultrawidelock));

	printf("\n== channel assignment ==\n");
	okc("remote.green", remote.g == LOCK_LED_BRIGHTNESS);
	okc("remote.blue_off", remote.b == 0);
	okc("ultrawidelock.blue", ultrawidelock.b == LOCK_LED_BRIGHTNESS);
	okc("ultrawidelock.green_off", ultrawidelock.g == 0);
	/* Red is unused by this policy; a nonzero red would wash both colours
	 * toward white and destroy the at-a-glance distinction. */
	okc("remote.red_off", remote.r == 0);
	okc("ultrawidelock.red_off", ultrawidelock.r == 0);

	printf("\n== the two unlock paths stay distinguishable ==\n");
	okc("paths.differ", remote.g != ultrawidelock.g || remote.b != ultrawidelock.b);
	okc("remote.single_channel", (remote.g != 0) && (remote.b == 0));
	okc("ultrawidelock.single_channel", (ultrawidelock.b != 0) && (ultrawidelock.g == 0));

	printf("\n== brightness is sane ==\n");
	/* Zero would compile and "pass" a lit/dark check while leaving the board
	 * dark on every unlock. */
	okc("brightness.nonzero", LOCK_LED_BRIGHTNESS > 0);
	okc("brightness.in_range", LOCK_LED_BRIGHTNESS <= 255);

	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails,
	       fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
