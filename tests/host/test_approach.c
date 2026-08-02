/** @file test_approach.c — predictive approach controller ("negative
 * latency"): ETA-scheduled fire timing, abort invariants, and the preserved
 * median/dwell presence path, replayed over synthetic walk-up profiles —
 * brisk walk, slow shuffle, stop-and-turn, hallway stationary, spike storms.
 * Samples arrive on the real 192 ms ranging-block grid with deterministic
 * uniform jitter, so every run is bit-identical. */
#include <string.h>

#include "aliro_approach.h"
#include "test.h"

#define BLOCK_MS 192

/* xorshift32: deterministic jitter, reseeded per profile. */
static uint32_t s_rng;

static uint32_t rng32(void)
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 17;
	s_rng ^= s_rng << 5;
	return s_rng;
}

static int32_t jitter(int32_t amp)
{
	return (int32_t)(rng32() % (uint32_t)(2 * amp + 1)) - amp;
}

/* Event recorder: count + first-occurrence time of each transition. */
struct rec {
	int n_predict, n_thresh, n_depart, n_abort;
	int64_t t_predict, t_thresh, t_depart, t_abort;
};

static void note(struct rec *r, enum aliro_approach_action a, int64_t t)
{
	switch (a) {
	case ALIRO_APPROACH_UNLOCK_PREDICT:
		if (r->n_predict++ == 0) {
			r->t_predict = t;
		}
		break;
	case ALIRO_APPROACH_UNLOCK_THRESHOLD:
		if (r->n_thresh++ == 0) {
			r->t_thresh = t;
		}
		break;
	case ALIRO_APPROACH_RELOCK_DEPART:
		if (r->n_depart++ == 0) {
			r->t_depart = t;
		}
		break;
	case ALIRO_APPROACH_RELOCK_ABORT:
		if (r->n_abort++ == 0) {
			r->t_abort = t;
		}
		break;
	default:
		break;
	}
}

/* A simulated walk-up: true distance moves linearly, one sample per block. */
struct walk {
	struct aliro_approach ap;
	struct rec r;
	int64_t t; /* ms of the last fed sample */
	float d;   /* true distance, cm */
};

static void walk_init(struct walk *w, uint32_t seed, float d0)
{
	memset(w, 0, sizeof(*w));
	aliro_approach_init(&w->ap, NULL);
	s_rng = seed;
	w->d = d0;
}

/* Same, with an explicit config (used to run a walk with the prediction path
 * compiled in but disabled, as the RSSI-power-gate builds do). */
static void walk_init_cfg(struct walk *w, uint32_t seed, float d0,
			  const struct aliro_approach_cfg *cfg)
{
	memset(w, 0, sizeof(*w));
	aliro_approach_init(&w->ap, cfg);
	s_rng = seed;
	w->d = d0;
}

/* One ranging block: move at v (cm/s, >0 = toward the door), then feed a
 * jittered sample (or the given override, for spike injection). */
static void walk_block(struct walk *w, float v_cm_s, int32_t noise,
		       int32_t override_cm)
{
	w->t += BLOCK_MS;
	w->d -= v_cm_s * (float)BLOCK_MS / 1000.0f;
	int32_t cm = override_cm >= 0 ? override_cm
				      : (int32_t)w->d + jitter(noise);

	note(&w->r, aliro_approach_feed(&w->ap, w->t, cm), w->t);
}

/* Feed blocks until the true distance passes d_to (either direction). Every
 * spike_every-th block (0 = never) sends spike_cm instead of the range. */
static void walk_until(struct walk *w, float v_cm_s, int32_t noise,
		       float d_to, int spike_every, int32_t spike_cm)
{
	int i = 0;

	while (v_cm_s > 0.0f ? w->d > d_to : w->d < d_to) {
		i++;
		walk_block(w, v_cm_s, noise,
			   (spike_every > 0 && i % spike_every == 0)
				   ? spike_cm
				   : -1);
	}
}

/* n blocks at fixed velocity (v may be 0: standing still). */
static void walk_blocks(struct walk *w, float v_cm_s, int32_t noise, int n)
{
	for (int i = 0; i < n; i++) {
		walk_block(w, v_cm_s, noise, -1);
	}
}

void test_approach(void)
{
	struct walk w;
	struct aliro_approach_cfg cfg;

	t_group("defaults + idle accessors");
	aliro_approach_defaults(&cfg);
	T_EQ("def.unlock_cm", cfg.unlock_cm, 100);
	T_EQ("def.relock_cm", cfg.relock_cm, 250);
	T_EQ("def.near_dwell", cfg.near_dwell, 2);
	T_EQ("def.far_dwell", cfg.far_dwell, 3);
	T_EQ("def.motor_ms", cfg.motor_ms, 500);
	T_EQ("def.margin_ms", cfg.margin_ms, 250);
	T_EQ("def.vmin_cm_s", cfg.vmin_cm_s, 30);
	T_EQ("def.far_silence_ms", cfg.far_silence_ms, 1500);
	walk_init(&w, 1, 600.0f);
	T_EQ("idle.cfg.null.defaults", w.ap.cfg.unlock_cm, 100);
	T_OK("idle.locked", aliro_approach_locked(&w.ap));
	T_EQ("idle.est", aliro_approach_est_cm(&w.ap), -1);
	T_EQ("idle.vel", aliro_approach_vel_cm_s(&w.ap), 0);
	T_EQ("idle.eta", aliro_approach_eta_ms(&w.ap), -1);

	t_group("brisk walk 1.3 m/s: bolt open before the hand arrives");
	/* True ring (100 cm) crossing at (600-100)/130 = 3846 ms; reach
	 * (60 cm) at 4153 ms. The prediction must fire once, no aborts, and
	 * fire + motor (500 ms) must land before reach — that is the negative
	 * latency: retraction DONE by arrival, not started at it. */
	walk_init(&w, 0xB01DFACE, 600.0f);
	walk_until(&w, 130.0f, 25, 300.0f, 0, 0);
	T_OK("brisk.mid.est", aliro_approach_est_cm(&w.ap) > 230 &&
				      aliro_approach_est_cm(&w.ap) < 380);
	T_OK("brisk.mid.vel", aliro_approach_vel_cm_s(&w.ap) > 80 &&
				      aliro_approach_vel_cm_s(&w.ap) < 180);
	walk_until(&w, 130.0f, 25, 55.0f, 0, 0);
	T_EQ("brisk.predict.once", w.r.n_predict, 1);
	T_EQ("brisk.thresh.none", w.r.n_thresh, 0);
	T_EQ("brisk.abort.none", w.r.n_abort, 0);
	T_OK("brisk.unlocked", !aliro_approach_locked(&w.ap));
	T_OK("brisk.done.by.reach", w.r.t_predict + 500 <= 4153);
	T_OK("brisk.fire.bounded", w.r.t_predict >= 3846 - 1500);

	t_group("spike storm: metre-scale outliers shift nothing");
	/* Same brisk walk, every 6th block replaced by a 16 m spike (the
	 * bench-observed failure mode). Guarantees must hold unchanged. */
	walk_init(&w, 0xB01DFACE, 600.0f);
	walk_until(&w, 130.0f, 25, 55.0f, 6, 1600);
	T_EQ("spike.predict.once", w.r.n_predict, 1);
	T_EQ("spike.abort.none", w.r.n_abort, 0);
	T_OK("spike.done.by.reach", w.r.t_predict + 500 <= 4153 + BLOCK_MS);
	T_OK("spike.fire.bounded", w.r.t_predict >= 3846 - 1500);

	t_group("near spoof while stationary: no fire from downward spikes");
	/* Standing at 3 m; every 5th sample claims 70 cm. The gate drops them
	 * and the median outvotes them: the bolt never moves. */
	walk_init(&w, 0x5EED0005, 300.0f);
	for (int i = 1; i <= 100; i++) {
		walk_block(&w, 0.0f, 20, i % 5 == 0 ? 70 : -1);
	}
	T_EQ("spoof.predict.none", w.r.n_predict, 0);
	T_EQ("spoof.thresh.none", w.r.n_thresh, 0);
	T_OK("spoof.locked", aliro_approach_locked(&w.ap));

	t_group("slow shuffle 0.15 m/s: threshold path still unlocks");
	/* Below vmin (30 cm/s) the predictor stays quiet; the shipped
	 * median/dwell presence unlock carries the case. Ring crossing at
	 * (220-100)/15 = 8000 ms. */
	walk_init(&w, 0xCAFE0003, 220.0f);
	walk_until(&w, 15.0f, 20, 70.0f, 0, 0);
	T_EQ("shuffle.predict.none", w.r.n_predict, 0);
	T_EQ("shuffle.thresh.once", w.r.n_thresh, 1);
	T_OK("shuffle.unlocked", !aliro_approach_locked(&w.ap));
	T_OK("shuffle.at.crossing", w.r.t_thresh >= 7000 &&
					    w.r.t_thresh <= 10500);

	t_group("hallway stationary at 1.6 m: never pre-actuates");
	walk_init(&w, 0xDEAD0004, 160.0f);
	walk_blocks(&w, 0.0f, 25, 156); /* ~30 s */
	T_EQ("hall.predict.none", w.r.n_predict, 0);
	T_EQ("hall.thresh.none", w.r.n_thresh, 0);
	T_OK("hall.locked", aliro_approach_locked(&w.ap));

	t_group("standing inside the ring: presence unlock preserved");
	walk_init(&w, 0xFEED0006, 80.0f);
	walk_blocks(&w, 0.0f, 15, 20);
	T_EQ("stand.thresh.once", w.r.n_thresh, 1);
	T_EQ("stand.predict.none", w.r.n_predict, 0);
	T_OK("stand.fast", w.r.t_thresh <= 1000);

	t_group("stop short after fire: predictive open aborts");
	/* Fires on the approach, then the walker halts at ~1.7 m and stands.
	 * Closing-speed decay (or the overdue deadline) must relock well
	 * before the peer would even time out. */
	walk_init(&w, 0xAB007007, 600.0f);
	walk_until(&w, 130.0f, 25, 170.0f, 0, 0);
	T_EQ("stop.predict.once", w.r.n_predict, 1);
	int64_t t_stop = w.t;

	walk_blocks(&w, 0.0f, 25, 20); /* stand for ~3.8 s */
	T_EQ("stop.abort.once", w.r.n_abort, 1);
	T_EQ("stop.thresh.none", w.r.n_thresh, 0);
	T_OK("stop.relocked", aliro_approach_locked(&w.ap));
	T_OK("stop.abort.fast", w.r.t_abort - t_stop <= 2500);

	t_group("departure: walk away relocks via far dwell");
	walk_init(&w, 0xD00D0008, 400.0f);
	walk_until(&w, 130.0f, 20, 70.0f, 0, 0);
	T_OK("depart.unlocked", !aliro_approach_locked(&w.ap));
	walk_until(&w, -160.0f, 20, 420.0f, 0, 0);
	T_EQ("depart.once", w.r.n_depart, 1);
	T_OK("depart.relocked", aliro_approach_locked(&w.ap));
	T_EQ("depart.gone.idle", aliro_approach_gone(&w.ap),
	     ALIRO_APPROACH_HOLD);

	t_group("peer gone while open: relock + reset");
	walk_init(&w, 0xD00D0009, 400.0f);
	walk_until(&w, 130.0f, 20, 70.0f, 0, 0);
	T_OK("gone.unlocked", !aliro_approach_locked(&w.ap));
	T_EQ("gone.relock", aliro_approach_gone(&w.ap),
	     ALIRO_APPROACH_RELOCK_DEPART);
	T_OK("gone.locked", aliro_approach_locked(&w.ap));
	T_EQ("gone.est.reset", aliro_approach_est_cm(&w.ap), -1);

	t_group("ranging dies after fire: overdue tick aborts");
	walk_init(&w, 0xF00D000A, 600.0f);
	for (int i = 0; i < 40 && w.r.n_predict == 0; i++) {
		walk_block(&w, 130.0f, 25, -1);
	}
	T_EQ("overdue.fired", w.r.n_predict, 1);
	int32_t eta = aliro_approach_eta_ms(&w.ap);

	T_OK("overdue.eta.sane", eta >= 0 && eta <= 750);
	for (int k = 1; k <= 20 && w.r.n_abort == 0; k++) {
		int64_t tk = w.t + (int64_t)k * 200;

		note(&w.r, aliro_approach_tick(&w.ap, tk), tk);
	}
	T_EQ("overdue.abort.once", w.r.n_abort, 1);
	T_OK("overdue.by.deadline",
	     w.r.t_abort - w.r.t_predict <= (int64_t)eta + 900 + 250);
	T_OK("overdue.relocked", aliro_approach_locked(&w.ap));

	t_group("teleport: gate rejects, then re-bases; predictor disarms");
	walk_init(&w, 0x7E1E000B, 500.0f);
	walk_blocks(&w, 0.0f, 20, 10);
	w.d = 150.0f; /* impossible jump; 3 agreeing samples re-base */
	walk_blocks(&w, 0.0f, 20, 20);
	T_EQ("tele.predict.none", w.r.n_predict, 0);
	T_EQ("tele.thresh.none", w.r.n_thresh, 0); /* dead band */
	T_OK("tele.est.follows", aliro_approach_est_cm(&w.ap) > 120 &&
					 aliro_approach_est_cm(&w.ap) < 180);
	T_OK("tele.vel.settled", aliro_approach_vel_cm_s(&w.ap) > -25 &&
					 aliro_approach_vel_cm_s(&w.ap) < 25);

	t_group("estimator edges: stale gap re-bases, zero-dt clamps");
	walk_init(&w, 0x0C0C000C, 0.0f);
	note(&w.r, aliro_approach_feed(&w.ap, 1000, 400), 1000);
	note(&w.r, aliro_approach_feed(&w.ap, 2600, 380), 2600); /* > stale */
	T_EQ("stale.rebased.est", aliro_approach_est_cm(&w.ap), 380);
	T_EQ("stale.rebased.vel", aliro_approach_vel_cm_s(&w.ap), 0);
	note(&w.r, aliro_approach_feed(&w.ap, 2600, 380), 2600); /* dt clamp */
	T_OK("clamp.est.stable", aliro_approach_est_cm(&w.ap) > 370 &&
					 aliro_approach_est_cm(&w.ap) < 390);

	t_group("predict_en=0: prediction path off, presence path untouched");
	/* The RSSI-power-gate build. Same brisk walk that fires a prediction
	 * above; with the path disabled the bolt must still open, but only on
	 * the shipped threshold rule, and no ETA may ever be published. */
	aliro_approach_defaults(&cfg);
	T_OK("def.predict_en", cfg.predict_en);
	cfg.predict_en = false;
	walk_init_cfg(&w, 0xB01DFACE, 600.0f, &cfg);
	walk_until(&w, 130.0f, 25, 55.0f, 0, 0);
	/* The estimator itself keeps running: `vel` still feeds the lab trace,
	 * it just cannot arm the bolt. */
	T_OK("off.vel.tracked", aliro_approach_vel_cm_s(&w.ap) > 80 &&
					aliro_approach_vel_cm_s(&w.ap) < 180);
	T_EQ("off.eta.never", aliro_approach_eta_ms(&w.ap), -1);
	/* Arriving and stopping: without the predictor the median has to settle
	 * inside the ring first, which is precisely the latency the prediction
	 * path exists to remove. */
	walk_blocks(&w, 0.0f, 25, 5);
	T_EQ("off.predict.none", w.r.n_predict, 0);
	T_EQ("off.abort.none", w.r.n_abort, 0);
	T_EQ("off.thresh.once", w.r.n_thresh, 1);
	T_OK("off.unlocked", !aliro_approach_locked(&w.ap));
	/* Departure still relocks on the presence path. */
	walk_until(&w, -130.0f, 25, 400.0f, 0, 0);
	T_EQ("off.depart.once", w.r.n_depart, 1);
	T_OK("off.relocked", aliro_approach_locked(&w.ap));
	/*
	 * The walk-away that hardware actually produces: a couple of far
	 * samples and then nothing, because the phone leaves UWB range or iOS
	 * stops ranging. far_dwell needs three and never gets them.
	 */
	t_group("departure by silence: two far samples then quiet still relocks");
	{
		struct walk d;

		walk_init(&d, 7, 600.0f);
		walk_until(&d, 130.0f, 10, 40.0f, 0, 0); /* arrive and unlock */
		T_OK("silence.unlocked", !aliro_approach_locked(&d.ap));

		/* Two far readings -- one short of far_dwell -- then silence. */
		d.t += BLOCK_MS;
		note(&d.r, aliro_approach_feed(&d.ap, d.t, 380), d.t);
		d.t += BLOCK_MS;
		note(&d.r, aliro_approach_feed(&d.ap, d.t, 420), d.t);
		T_OK("silence.still.open.after.two", !aliro_approach_locked(&d.ap));

		T_EQ("silence.nothing.before.the.window",
		     (long)aliro_approach_tick(&d.ap, d.t + 1000),
		     (long)ALIRO_APPROACH_HOLD);
		T_EQ("silence.relocks.after.it",
		     (long)aliro_approach_tick(&d.ap, d.t + 1500),
		     (long)ALIRO_APPROACH_RELOCK_DEPART);
		T_OK("silence.locked", aliro_approach_locked(&d.ap));
		T_EQ("silence.only.once", (long)aliro_approach_tick(&d.ap, d.t + 9000),
		     (long)ALIRO_APPROACH_HOLD);
	}

	/*
	 * The case the gate protects: a phone put down INSIDE the ring also
	 * stops ranging, and relocking under its owner is the worse failure.
	 */
	t_group("silence while near: the bolt stays put");
	{
		struct walk n;

		walk_init(&n, 11, 600.0f);
		walk_until(&n, 130.0f, 10, 40.0f, 0, 0);
		T_OK("near.unlocked", !aliro_approach_locked(&n.ap));

		T_EQ("near.silence.holds", (long)aliro_approach_tick(&n.ap, n.t + 60000),
		     (long)ALIRO_APPROACH_HOLD);
		T_OK("near.still.open", !aliro_approach_locked(&n.ap));
	}

	/* far_silence_ms == 0 restores the sample-only behaviour exactly. */
	t_group("departure by silence: disabled by config");
	{
		struct walk z;
		struct aliro_approach_cfg zcfg;

		aliro_approach_defaults(&zcfg);
		zcfg.far_silence_ms = 0;
		walk_init_cfg(&z, 13, 600.0f, &zcfg);
		walk_until(&z, 130.0f, 10, 40.0f, 0, 0);
		z.t += BLOCK_MS;
		note(&z.r, aliro_approach_feed(&z.ap, z.t, 380), z.t);

		T_EQ("off.silence.never.fires", (long)aliro_approach_tick(&z.ap, z.t + 60000),
		     (long)ALIRO_APPROACH_HOLD);
		T_OK("off.still.open", !aliro_approach_locked(&z.ap));
	}
}
