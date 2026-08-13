/*
 * The grant decision. See grant.h for why it is not inline in the loop.
 *
 * A LINE-FOR-LINE PORT of the Zephyr image's reader loop, minus the I/O and
 * minus two subsystems that are not on this board. The ordering is load
 * bearing and is preserved exactly: feed or tick, then act, then the session
 * edge, then the departure fallback. Moving the fallback above the action
 * would relock on the same pass that granted.
 */
#include <stddef.h>

#include "grant.h"

void grant_init(struct grant_ctx *ctx, uint32_t gen_now)
{
	/*
	 * Factory defaults: unlock 100 cm, relock 250 cm, and a trajectory gate
	 * at 180 cm -- no auto-unlock until the credential has been seen that
	 * far out in this session, so a phone already at the door when ranging
	 * started does not open it.
	 */
	ultrawidelock_approach_init(&ctx->approach, NULL);
	ctx->gen_trusted = gen_now;
	ctx->gen_observed = gen_now;
	ctx->gen_led = gen_now;
	ctx->last_range_ms = 0;
	ctx->present = false;
	ctx->granted = false;
	ctx->session_was_up = false;
}

void grant_step(struct grant_ctx *ctx, const struct grant_input *in, struct grant_output *out)
{
	enum ultrawidelock_approach_action act;

	out->lock_changed = false;
	out->unlocked = ctx->granted;
	out->departure_fallback = false;

	/*
	 * One approach sample per NEWLY accepted trusted range. A stale latch
	 * keeps the old generation -- iOS stops ranging once the phone holds
	 * still -- so it drives a tick, not a fresh sample.
	 */
	if (in->gen != ctx->gen_trusted && in->trusted_valid) {
		ctx->gen_trusted = in->gen;
		ctx->gen_observed = in->gen;
		ctx->present = true;
		act = ultrawidelock_approach_feed(&ctx->approach, in->now_ms, in->trusted_cm);
	} else {
		/*
		 * A fresh range the consensus will not vouch for still says
		 * something, about DEPARTURE only: far ranges are the ones it
		 * declines, so without this the walk-away relock can never fire.
		 *
		 * gen_trusted is deliberately NOT consumed here, so a range that
		 * becomes trusted a block later is still taken by the branch
		 * above.
		 */
		if (in->gen != ctx->gen_observed) {
			ctx->gen_observed = in->gen;
			if (in->raw_valid) {
				ultrawidelock_approach_observe_departure(&ctx->approach, in->now_ms,
								 in->raw_cm);
			}
		}
		act = ultrawidelock_approach_tick(&ctx->approach, in->now_ms);
	}

	out->action = act;

	switch (act) {
	case ULTRAWIDELOCK_APPROACH_UNLOCK_PREDICT:
	case ULTRAWIDELOCK_APPROACH_UNLOCK_THRESHOLD:
		/*
		 * NO SIDE GATE ON THIS IMAGE. The Zephyr build can suppress a
		 * passive unlock on inside/outside evidence from witness links;
		 * that subsystem is not ported, so every passive grant here is
		 * ungated. Same as a Zephyr board built without
		 * CONFIG_ULTRAWIDELOCK_SIDE_GATE, which is the shipped default.
		 */
		if (!ctx->granted) {
			out->lock_changed = true;
		}
		ctx->granted = true;
		out->unlocked = true;
		break;
	case ULTRAWIDELOCK_APPROACH_RELOCK_DEPART:
	case ULTRAWIDELOCK_APPROACH_RELOCK_ABORT:
		if (ctx->granted) {
			out->lock_changed = true;
		}
		ctx->granted = false;
		out->unlocked = false;
		break;
	default:
		break;
	}

	/*
	 * A session cannot come up without the phone approaching: the BLE RSSI
	 * power gate holds ranging off until the connection crosses its open
	 * threshold. So this edge is the approach evidence the trajectory gate
	 * wants, and it is the only form of it this architecture produces --
	 * UWB starts when the phone is already at the door, so a 180 cm range
	 * never arrives.
	 */
	if (in->session_active && !ctx->session_was_up) {
		ultrawidelock_approach_session_up(&ctx->approach);
	}
	ctx->session_was_up = in->session_active;

	/*
	 * Departure: the peer's credential session ended. Ranging silence alone does
	 * NOT mean departed, because a still phone stops ranging too, so this
	 * gates on the session and not on range age.
	 *
	 * Reaching here with the bolt still open means the silence relock did
	 * not fire, and this is the only moment that proves it. The flag is
	 * raised BEFORE ultrawidelock_approach_gone(), which re-inits the struct and
	 * erases the evidence the caller wants to log.
	 */
	if (ctx->present && !in->session_active) {
		out->departure_fallback = ctx->granted;
		(void)ultrawidelock_approach_gone(&ctx->approach);
		if (ctx->granted) {
			ctx->granted = false;
			out->lock_changed = true;
			out->unlocked = false;
		}
		ctx->present = false;
	}

	if (in->gen != ctx->gen_led) {
		ctx->gen_led = in->gen;
		ctx->last_range_ms = in->now_ms;
	}
	/*
	 * The != 0 is not redundant: uptime is a few tens of milliseconds when
	 * this first runs, so without it every board reports ranging for its
	 * first second.
	 */
	out->ranging = ctx->last_range_ms != 0 &&
		       (in->now_ms - ctx->last_range_ms) < GRANT_RANGE_HOLD_MS;
}
