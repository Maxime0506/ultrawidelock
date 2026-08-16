/** @file test_grant.c — the grant decision: the glue between an accepted range
 * and the bolt. grant_step() is pure with respect to everything but its ctx, so
 * every property its comments call load bearing is checkable here without a
 * radio, a GPIO or a scheduler.
 *
 * What this pins, in the order grant.c does it: the two generation epochs and
 * why they are separate, the stale-latch tick, the lock_changed edge, the
 * departure fallback sitting BELOW the action, and the ranging-lamp hold.
 *
 * The probe for "did a real approach sample happen" is
 * ultrawidelock_approach_est_cm(): it returns -1 until the Kalman filter is
 * initialised, and ultrawidelock_approach_observe_departure() deliberately does
 * not initialise it. So est_cm still reading -1 after a pass proves that pass
 * took the tick branch rather than the feed branch.
 */
#include <string.h>

#include "grant.h"
#include "test.h"

#define BLOCK_MS 192

/* One loop pass, assembled. The rig owns the clock and the latch counter so a
 * test names only what it is varying. */
struct rig {
	struct grant_ctx ctx;
	struct grant_output out;
	int64_t t;
	uint32_t gen;
};

static void rig_init(struct rig *g)
{
	memset(g, 0, sizeof(*g));
	grant_init(&g->ctx, 0);
	g->t = 0;
	g->gen = 0;
}

/* A pass with an explicit input; the rig's clock is used as-is. */
static void pass(struct rig *g, const struct grant_input *in)
{
	grant_step(&g->ctx, in, &g->out);
}

/* Advance one ranging block and deliver a NEW vouched range. */
static void block_trusted(struct rig *g, int32_t cm, bool session)
{
	struct grant_input in;

	g->t += BLOCK_MS;
	g->gen++;
	memset(&in, 0, sizeof(in));
	in.now_ms = g->t;
	in.gen = g->gen;
	in.trusted_valid = true;
	in.trusted_cm = cm;
	in.raw_valid = true;
	in.raw_cm = cm;
	in.session_active = session;
	pass(g, &in);
}

/* Advance one block with no new latch at all: the still-phone case. */
static void block_stale(struct rig *g, bool session)
{
	struct grant_input in;

	g->t += BLOCK_MS;
	memset(&in, 0, sizeof(in));
	in.now_ms = g->t;
	in.gen = g->gen; /* unchanged: iOS stopped ranging */
	in.trusted_valid = true;
	in.trusted_cm = 40;
	in.raw_valid = true;
	in.raw_cm = 40;
	in.session_active = session;
	pass(g, &in);
}

/* Walk in from d0 to d_to at v cm/s, one vouched sample per block. Returns the
 * number of passes in which the bolt changed state. */
static int walk_in(struct rig *g, int32_t d0, int32_t d_to, int32_t v_cm_s, bool session)
{
	int32_t d = d0;
	int changes = 0;

	while (d > d_to) {
		d -= v_cm_s * BLOCK_MS / 1000;
		block_trusted(g, d, session);
		if (g->out.lock_changed) {
			changes++;
		}
	}
	return changes;
}

/* The walk-up used by the ordering test, as a replayable sequence. No jitter
 * and integer steps, so block n carries the same range on every run: that is
 * what lets one pass be singled out by index. Blocks are numbered from 1.
 * @p drop_at is the one block delivered with the session already down. */
#define WALK_D0   600
#define WALK_V    130
#define WALK_STEP (WALK_V * BLOCK_MS / 1000)
#define WALK_MAX  64

static void walk_replay(struct rig *g, int drop_at, int stop_after)
{
	int32_t d = WALK_D0;

	rig_init(g);
	for (int n = 1; n <= stop_after; n++) {
		d -= WALK_STEP;
		block_trusted(g, d, n != drop_at);
	}
}

/* Index of the first block whose pass opens the bolt, session up throughout.
 * -1 if the walk never grants, which would make the ordering test vacuous. */
static int walk_grant_block(void)
{
	struct rig g;
	int32_t d = WALK_D0;

	rig_init(&g);
	for (int n = 1; n <= WALK_MAX; n++) {
		d -= WALK_STEP;
		block_trusted(&g, d, true);
		if (g.out.lock_changed && g.out.unlocked) {
			return n;
		}
	}
	return -1;
}

void test_grant(void)
{
	struct rig g;
	struct grant_input in;

	t_group("init: nothing granted, nothing ranging");
	rig_init(&g);
	T_OK("init.locked", !g.ctx.granted);
	T_OK("init.absent", !g.ctx.present);
	T_OK("init.session.down", !g.ctx.session_was_up);
	T_EQ("init.est.none", ultrawidelock_approach_est_cm(&g.ctx.approach), -1);

	/*
	 * The `last_range_ms != 0` guard in grant.c. Uptime is a few tens of ms
	 * on the first pass, so without it `now - 0 < 1000` is true and every
	 * board claims to be ranging for its first second.
	 */
	t_group("ranging lamp: the first-second guard and the hold window");
	rig_init(&g);
	memset(&in, 0, sizeof(in));
	in.now_ms = 30; /* early uptime, no latch yet: gen still 0 */
	in.gen = 0;
	pass(&g, &in);
	T_OK("lamp.boot.dark", !g.out.ranging);

	memset(&in, 0, sizeof(in));
	in.now_ms = 5000;
	in.gen = 1;
	in.trusted_valid = true;
	in.trusted_cm = 400;
	pass(&g, &in);
	T_OK("lamp.lit.on.latch", g.out.ranging);

	in.now_ms = 5000 + GRANT_RANGE_HOLD_MS - 1; /* still inside the hold */
	pass(&g, &in);
	T_OK("lamp.held.inside", g.out.ranging);

	in.now_ms = 5000 + GRANT_RANGE_HOLD_MS; /* the boundary is exclusive */
	pass(&g, &in);
	T_OK("lamp.dark.at.hold", !g.out.ranging);

	/*
	 * gen_trusted is NOT consumed by the untrusted branch. The good-run
	 * counter builds across blocks, so a range that the consensus declined
	 * when first seen can become vouched a block later, and the trusted
	 * epoch has to still be open for it. Consuming it here is the bug this
	 * pins: the late-arriving trust would be swallowed as a stale latch and
	 * the approach controller would never see the sample.
	 */
	t_group("late trust: an unvouched pass leaves the trusted epoch open");
	rig_init(&g);
	memset(&in, 0, sizeof(in));
	in.now_ms = BLOCK_MS;
	in.gen = 1;
	in.trusted_valid = false;
	in.raw_valid = true;
	in.raw_cm = 500; /* >= relock_cm, so departure evidence registers */
	in.session_active = true;
	pass(&g, &in);
	T_EQ("late.unvouched.no.feed", ultrawidelock_approach_est_cm(&g.ctx.approach), -1);
	T_OK("late.unvouched.not.present", !g.ctx.present);

	in.now_ms = 2 * BLOCK_MS;
	in.gen = 1; /* the SAME latch, now vouched for */
	in.trusted_valid = true;
	in.trusted_cm = 500;
	pass(&g, &in);
	T_OK("late.vouched.feeds", ultrawidelock_approach_est_cm(&g.ctx.approach) > 0);
	T_OK("late.vouched.present", g.ctx.present);

	/*
	 * The mirror case: once a latch HAS been fed, repeating it must drive a
	 * tick and not a second sample. Counting a still phone's held latch as
	 * fresh evidence refreshes the silence clock, and a clock that never
	 * expires is a walk-away that never relocks.
	 */
	t_group("stale latch: a held generation ticks, it does not re-feed");
	rig_init(&g);
	block_trusted(&g, 500, true);
	T_OK("stale.first.feeds", ultrawidelock_approach_est_cm(&g.ctx.approach) > 400);
	for (int i = 0; i < 10; i++) {
		block_stale(&g, true); /* claims 40 cm, on the same latch */
	}
	T_OK("stale.est.unmoved", ultrawidelock_approach_est_cm(&g.ctx.approach) > 400);
	T_OK("stale.no.grant", !g.ctx.granted);

	t_group("walk-up: the bolt opens once, on an edge");
	rig_init(&g);
	/*
	 * The session edge is the approach evidence the trajectory gate wants.
	 * UWB starts once the phone is already near the door, so the 180 cm
	 * sighting the gate would otherwise need never arrives.
	 */
	int changes = walk_in(&g, 600, 40, 130, true);
	T_OK("walk.unlocked", g.ctx.granted);
	T_OK("walk.out.unlocked", g.out.unlocked);
	T_EQ("walk.edge.once", changes, 1);
	T_OK("walk.session.up", g.ctx.session_was_up);

	/* Level, not edge: standing at the door does not re-fire lock_changed. */
	for (int i = 0; i < 5; i++) {
		block_trusted(&g, 40, true);
		T_OK("walk.hold.no.edge", !g.out.lock_changed);
		T_OK("walk.hold.still.open", g.out.unlocked);
	}

	/*
	 * Ordering. The fallback sits BELOW the action switch in grant.c; moving
	 * it above would relock on the same pass that granted. And the flag is
	 * raised BEFORE ultrawidelock_approach_gone(), which re-inits the struct
	 * and erases the evidence the caller wants to log.
	 */
	t_group("departure fallback: session ends with the bolt still open");
	block_trusted(&g, 40, false); /* session drops, still granted */
	T_OK("depart.fallback.raised", g.out.departure_fallback);
	T_OK("depart.relocked", !g.out.unlocked);
	T_OK("depart.edge", g.out.lock_changed);
	T_OK("depart.ctx.locked", !g.ctx.granted);
	T_OK("depart.ctx.absent", !g.ctx.present);
	T_EQ("depart.approach.reset", ultrawidelock_approach_est_cm(&g.ctx.approach), -1);

	t_group("departure fallback: silent when the bolt was already shut");
	rig_init(&g);
	block_trusted(&g, 500, true); /* present, never granted */
	T_OK("quiet.present", g.ctx.present);
	block_trusted(&g, 500, false); /* session ends, bolt shut */
	T_OK("quiet.no.fallback", !g.out.departure_fallback);
	T_OK("quiet.no.edge", !g.out.lock_changed);
	T_OK("quiet.absent", !g.ctx.present);

	/*
	 * The ordering itself, on the one pass that can tell the two orders
	 * apart: the block that would grant is delivered with the session
	 * already down. Correct order grants and then relocks on that same pass,
	 * so the bolt ends shut and the fallback is raised. With the fallback
	 * moved above the action switch it reads ctx->granted while it is still
	 * false, does nothing, and the switch then opens the bolt against a dead
	 * session -- which is the failure, not the reordering.
	 */
	t_group("ordering: a grant landing on a dead session must not stick");
	int n_grant = walk_grant_block();
	T_OK("order.walk.grants", n_grant > 0);
	/* Session up throughout: the same block does open the bolt. */
	walk_replay(&g, -1, n_grant);
	T_OK("order.control.open", g.out.unlocked);
	T_EQ("order.control.edge", g.out.lock_changed, 1);
	/* Same sequence, session down on exactly that block. */
	walk_replay(&g, n_grant, n_grant);
	T_OK("order.dead.session.shut", !g.out.unlocked);
	T_OK("order.dead.session.ctx.shut", !g.ctx.granted);
	T_OK("order.dead.session.fallback", g.out.departure_fallback);
	T_OK("order.dead.session.absent", !g.ctx.present);

	t_group("departure needs presence, not merely a dead session");
	rig_init(&g);
	memset(&in, 0, sizeof(in));
	in.now_ms = BLOCK_MS;
	in.gen = 1;
	in.session_active = false;
	pass(&g, &in);
	T_OK("never.present.no.fallback", !g.out.departure_fallback);
	T_OK("never.present.no.edge", !g.out.lock_changed);
}
