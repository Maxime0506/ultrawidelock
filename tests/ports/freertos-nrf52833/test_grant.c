/*
 * The grant decision, driven as walk-ups rather than as function calls.
 *
 * WHAT THIS COVERS THAT test_approach.c DOES NOT. That suite tests the
 * controller: given a sequence of ranges, does it decide to unlock. This tests
 * the code that chooses WHICH ranges the controller ever sees, which is a
 * separate question and, until this file, one no test on any port asked. The
 * three latch epochs and the departure fallback all live there, and each of
 * them is a place where a plausible simplification changes when a door relocks.
 *
 * So every case here is written as a defect: something that would still pass
 * test_approach.c, still compile, still boot, and still be wrong.
 */
#include <stdio.h>
#include <string.h>

#include "grant.h"

static unsigned g_checks;
static unsigned g_fails;

#define CHECK(desc, cond)                                                                          \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (cond) {                                                                        \
			printf("  ok   %s\n", (desc));                                             \
		} else {                                                                           \
			printf("  FAIL %s\n", (desc));                                             \
			g_fails++;                                                                 \
		}                                                                                  \
	} while (0)

/* One pass with everything spelled out, so a scenario reads as a walk-up. */
static struct grant_output step(struct grant_ctx *ctx, int64_t now_ms, uint32_t gen,
				bool trusted_valid, int32_t trusted_cm, bool raw_valid,
				int32_t raw_cm, bool session)
{
	struct grant_input in;
	struct grant_output out;

	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.now_ms = now_ms;
	in.gen = gen;
	in.trusted_valid = trusted_valid;
	in.trusted_cm = trusted_cm;
	in.raw_valid = raw_valid;
	in.raw_cm = raw_cm;
	in.session_active = session;
	grant_step(ctx, &in, &out);
	return out;
}

/*
 * A trusted range is consumed exactly once, however many passes see it.
 *
 * The loop runs far faster than ranges land -- four times a second at the very
 * least, and once per latch when a walk-up is live -- so the same generation is
 * observed repeatedly. Feeding it each time would multiply every walk-up's
 * sample count by however fast the loop happened to be running, which changes
 * the controller's answer without changing anything it was told about.
 */
static void check_trusted_consumed_once(void)
{
	struct grant_ctx ctx;
	int64_t t = 1000;

	printf("a stale generation is not re-fed\n");
	grant_init(&ctx, 0);

	/* One new latch at 300 cm, then four passes that see the same one. */
	(void)step(&ctx, t, 1, true, 300, true, 300, true);
	int32_t after_first = ctx.approach.last_cm;

	for (int i = 0; i < 4; i++) {
		t += 250;
		(void)step(&ctx, t, 1, true, 300, true, 300, true);
	}

	CHECK("the first trusted range reaches the controller", after_first == 300);
	CHECK("repeat passes over one generation change nothing",
	      ctx.gen_trusted == 1u && ctx.gen_observed == 1u);
}

/*
 * Trust arriving late for a latch already observed.
 *
 * The integrity consensus builds a good-run counter across blocks, so a range
 * that was unvouched when first seen can become trusted a moment later. The
 * observed epoch has to be able to move without closing the trusted one, which
 * is the entire reason they are two variables. Merge them and this range is
 * never fed.
 */
static void check_late_trust_still_feeds(void)
{
	struct grant_ctx ctx;

	printf("trust that arrives late is still fed\n");
	grant_init(&ctx, 0);

	/* Generation 1 lands unvouched: departure evidence only. */
	(void)step(&ctx, 1000, 1, false, 0, true, 400, true);
	CHECK("an unvouched range moves the observed epoch", ctx.gen_observed == 1u);
	CHECK("an unvouched range does NOT move the trusted epoch", ctx.gen_trusted == 0u);

	/* The same generation, now vouched for. */
	(void)step(&ctx, 1250, 1, true, 400, true, 400, true);
	CHECK("the trusted epoch catches up when trust arrives", ctx.gen_trusted == 1u);
	CHECK("the controller was fed the range it had refused", ctx.approach.last_cm == 400);
}

/*
 * A departure sample is observed once, not once per pass.
 *
 * Observing the same range repeatedly refreshes the silence clock, and a
 * silence clock that never expires means a walk-away never relocks. This is the
 * failure that made the two epochs necessary in the first place.
 */
static void check_departure_observed_once(void)
{
	struct grant_ctx ctx;
	struct grant_ctx ref;

	printf("one departure sample is observed once\n");
	grant_init(&ctx, 0);
	grant_init(&ref, 0);

	/* ctx sees generation 7 across six passes; ref sees it on one. */
	for (int i = 0; i < 6; i++) {
		(void)step(&ctx, 1000 + 250 * i, 7, false, 0, true, 500, true);
	}
	(void)step(&ref, 1000, 7, false, 0, true, 500, true);
	for (int i = 1; i < 6; i++) {
		/* Same clock, but no new generation at all. */
		(void)step(&ref, 1000 + 250 * i, 7, false, 0, false, 0, true);
	}

	CHECK("repeated passes leave the same observed epoch",
	      ctx.gen_observed == 7u && ref.gen_observed == 7u);
	CHECK("and the same controller state",
	      ctx.approach.last_cm == ref.approach.last_cm);
}

/*
 * The session edge arms the trajectory gate, and only on the rising edge.
 *
 * Calling it on every pass where a session is up would re-arm the gate
 * continuously, which is indistinguishable from having no gate.
 */
static void check_session_edge_is_an_edge(void)
{
	struct grant_ctx ctx;

	printf("the session edge fires once per session\n");
	grant_init(&ctx, 0);

	CHECK("no session at rest", !ctx.session_was_up);
	(void)step(&ctx, 1000, 0, false, 0, false, 0, true);
	CHECK("a session coming up is latched", ctx.session_was_up);
	(void)step(&ctx, 1250, 0, false, 0, false, 0, true);
	CHECK("a session staying up stays latched", ctx.session_was_up);
	(void)step(&ctx, 1500, 0, false, 0, false, 0, false);
	CHECK("a session ending clears the latch", !ctx.session_was_up);
}

/*
 * The departure fallback closes a bolt the silence relock left open, and says
 * so exactly once.
 *
 * Ranging silence alone is not departure -- a still phone stops ranging with
 * the session up -- so this has to gate on the session ending. Reaching it with
 * the bolt open is the only proof the silence relock did not fire, which is why
 * the flag exists at all.
 */
static void check_departure_fallback(void)
{
	struct grant_ctx ctx;
	struct grant_output out;
	int64_t t = 1000;
	uint32_t gen = 0;

	printf("the departure fallback closes an open bolt, once\n");
	grant_init(&ctx, 0);

	/* Walk in from 400 cm to inside the unlock threshold, with a session up
	 * the whole way so the trajectory gate is armed. */
	(void)step(&ctx, t, ++gen, true, 400, true, 400, true);
	for (int32_t cm = 380; cm >= 40; cm -= 20) {
		t += 200;
		out = step(&ctx, t, ++gen, true, cm, true, cm, true);
		if (out.unlocked) {
			break;
		}
	}
	CHECK("the walk-in opened the bolt", ctx.granted);
	CHECK("and the board considers a peer present", ctx.present);

	/* The session drops with the bolt still open. */
	t += 250;
	out = step(&ctx, t, gen, false, 0, false, 0, false);
	CHECK("the fallback reports itself", out.departure_fallback);
	CHECK("the bolt closes", !ctx.granted && out.lock_changed && !out.unlocked);
	CHECK("presence is cleared", !ctx.present);

	/* And does not report again on the next quiet pass. */
	t += 250;
	out = step(&ctx, t, gen, false, 0, false, 0, false);
	CHECK("it does not fire twice", !out.departure_fallback && !out.lock_changed);
}

/*
 * lock_changed is an edge, not a level.
 *
 * The caller notifies the reader and drives a lamp on it. A level would
 * re-notify Wallet on every pass for as long as the door stayed open, which is
 * four times a second at rest and once per range during a walk-up.
 */
static void check_lock_changed_is_an_edge(void)
{
	struct grant_ctx ctx;
	struct grant_output out;
	int64_t t = 1000;
	uint32_t gen = 0;
	int changes = 0;

	printf("lock_changed is an edge\n");
	grant_init(&ctx, 0);

	(void)step(&ctx, t, ++gen, true, 400, true, 400, true);
	for (int i = 0; i < 40; i++) {
		int32_t cm = 380 - 10 * i;

		if (cm < 30) {
			cm = 30;
		}
		t += 200;
		out = step(&ctx, t, ++gen, true, cm, true, cm, true);
		if (out.lock_changed) {
			changes++;
		}
	}
	CHECK("an open door is announced once, not once per pass",
	      ctx.granted ? changes == 1 : changes == 0);
}

/*
 * The ranging lamp does not light on a board that has never ranged.
 *
 * last_range_ms starts at zero and the clock starts near zero, so without an
 * explicit test for "never" every board reports ranging for its first second.
 */
static void check_ranging_lamp(void)
{
	struct grant_ctx ctx;
	struct grant_output out;

	printf("the ranging lamp\n");
	grant_init(&ctx, 0);

	out = step(&ctx, 12, 0, false, 0, false, 0, false);
	CHECK("dark on a board that has never ranged", !out.ranging);

	out = step(&ctx, 1000, 5, true, 200, true, 200, true);
	CHECK("lit when a range lands", out.ranging);

	out = step(&ctx, 1000 + GRANT_RANGE_HOLD_MS - 1, 5, false, 0, false, 0, true);
	CHECK("still lit inside the hold", out.ranging);

	out = step(&ctx, 1000 + GRANT_RANGE_HOLD_MS, 5, false, 0, false, 0, true);
	CHECK("dark once the hold expires", !out.ranging);
}

int main(void)
{
	check_trusted_consumed_once();
	check_late_trust_still_feeds();
	check_departure_observed_once();
	check_session_edge_is_an_edge();
	check_departure_fallback();
	check_lock_changed_is_an_edge();
	check_ranging_lamp();

	printf("RESULT: %s (%u checks)\n", g_fails == 0 ? "PASS" : "FAIL", g_checks);
	return g_fails == 0 ? 0 : 1;
}
