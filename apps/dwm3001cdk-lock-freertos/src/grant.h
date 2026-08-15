/* SPDX-License-Identifier: ISC */

/*
 * The grant decision, separated from the loop that feeds it.
 *
 * WHY THIS IS A FILE RATHER THAN THE BODY OF A WHILE LOOP. The logic here is
 * four pieces of state over one latch counter, and every one of them is subtle
 * enough that its comment in the Zephyr original explains what breaks if it is
 * merged with its neighbour. None of it was reachable from a test on either
 * port, because it lived inline in main() between a radio read and a GPIO
 * write. The approach controller underneath has a thousand lines of host tests;
 * the code deciding what to feed it had none.
 *
 * So the loop keeps the I/O -- sampling the radio, notifying the reader,
 * driving the lamps, logging -- and this takes a struct of what was observed
 * and returns a struct of what should happen. It calls nothing it does not
 * take as an argument, which is what makes a walk-up something a test can
 * write down.
 */
#ifndef ULTRAWIDELOCK_FREERTOS_GRANT_H
#define ULTRAWIDELOCK_FREERTOS_GRANT_H

#include <stdbool.h>
#include <stdint.h>

#include <ultrawidelock_approach.h>

struct grant_ctx {
	struct ultrawidelock_approach approach;
	/*
	 * Three epochs over the same latch counter, consumed at different
	 * moments on purpose.
	 *
	 * trusted is taken only by a range the integrity consensus vouches for.
	 * observed is taken by any fresh range at all, so a departure sample is
	 * never counted twice -- counting it twice refreshes the silence clock
	 * and stops it ever expiring, which means a walk-away never relocks.
	 * They are separate because trust can arrive LATE for a latch already
	 * taken: the good-run counter builds across blocks, so a range that was
	 * unvouched when first seen can become trusted a block later, and the
	 * trusted epoch has to still be open for it.
	 */
	uint32_t gen_trusted;
	uint32_t gen_observed;
	/* Read-only with respect to the unlock logic; drives the lamp alone. */
	uint32_t gen_led;
	int64_t last_range_ms;
	bool present;
	bool granted;
	bool session_was_up;
};

/* What one pass of the loop observed. Nothing here is read by the decision
 * except through this struct, which is the property the tests depend on. */
struct grant_input {
	int64_t now_ms;
	uint32_t gen;
	/* A range the integrity consensus vouches for. */
	bool trusted_valid;
	int32_t trusted_cm;
	/* The raw latch, vouched for or not. Departure evidence only. */
	bool raw_valid;
	int32_t raw_cm;
	bool session_active;
};

/* What the loop should now do. The decision performs no I/O itself. */
struct grant_output {
	/* True when the bolt state changed this pass; unlocked says which way. */
	bool lock_changed;
	bool unlocked;
	/* A range landed recently enough to show the ranging rate. */
	bool ranging;
	/* Set when the session ended with the bolt still open, which means the
	 * silence relock did not fire. The loop logs it; nothing else reads it. */
	bool departure_fallback;
	enum ultrawidelock_approach_action action;
};

/* How long one accepted range keeps the ranging lamp lit. Outlives one round
 * at every rate iOS uses, so a walk-up reads as steady rather than stuttering,
 * and it falls back to the session light about a second after a phone stops
 * ranging -- which a still phone does, with the session still up. */
#define GRANT_RANGE_HOLD_MS 1000

void grant_init(struct grant_ctx *ctx, uint32_t gen_now);

/* One pass. Pure with respect to everything but @p ctx. */
void grant_step(struct grant_ctx *ctx, const struct grant_input *in,
		struct grant_output *out);

#endif /* ULTRAWIDELOCK_FREERTOS_GRANT_H */
