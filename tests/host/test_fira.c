/** @file test_fira.c — range + URSK store (fira_session). */
#include <string.h>

#include "cred_kdf.h" /* ULTRAWIDELOCK_URSK_LEN */
#include "fira_session.h"
#include "test.h"

static int s_listener_hits;

static void range_listener(void)
{
	s_listener_hits++;
}

void test_fira(void)
{
	uint8_t ursk[ULTRAWIDELOCK_URSK_LEN];
	int32_t cm = -1;
	uint32_t block = 0;
	int64_t age = -1;

	for (size_t i = 0; i < sizeof(ursk); i++) {
		ursk[i] = (uint8_t)(i + 1u);
	}

	t_group("range store starts empty");
	fira_session_set_provisioned_ursk(NULL); /* also normalises state */
	T_OK("empty", !fira_session_last_range(&cm, NULL, NULL, NULL, NULL));

	t_group("URSK stash");
	T_OK("ursk.none", fira_session_get_ursk() == NULL);
	fira_session_set_provisioned_ursk(ursk);
	const uint8_t *g = fira_session_get_ursk();
	T_OK("ursk.set", g != NULL && memcmp(g, ursk, sizeof(ursk)) == 0);
	fira_session_set_provisioned_ursk(NULL);
	T_OK("ursk.cleared", fira_session_get_ursk() == NULL);

	t_group("diagnostic slot counter is inert");
	T_EQ("slot", fira_session_current_slot(), 0u);

	t_group("latch + read range");
	fira_session_set_ccc_range_cm(150, 7u);
	T_OK("present", fira_session_last_range(&cm, NULL, NULL, &block, &age));
	T_EQ("cm", cm, 150);
	T_EQ("block", block, 7u);
	T_OK("age.nonneg", age >= 0);
	/* Negative distance clamps to 0. */
	fira_session_set_ccc_range_cm(-5, 9u);
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("cm.clamped", cm, 0);
	T_EQ("block2", block, 9u);
	/* All out-params optional. */
	T_OK("null.outs", fira_session_last_range(NULL, NULL, NULL, NULL, NULL));
	/* All out-params filled: the CCC latch reports addr 0, LOS. */
	uint16_t addr = 0xFFFFu;
	uint8_t nlos = 0xFFu;
	T_OK("full.outs", fira_session_last_range(&cm, &addr, &nlos, &block, &age));
	T_EQ("addr", addr, 0u);
	T_EQ("nlos", nlos, 0u);

	/* ── Range-integrity gate ─────────────────────────────────────────────── */

	t_group("layer 1: plausibility predicate");
	T_OK("plaus.150", fira_session_range_plausible(150));
	T_OK("plaus.zero", fira_session_range_plausible(0));
	T_OK("plaus.small_neg", fira_session_range_plausible(-FIRA_RANGE_NEG_TOL_CM));
	T_OK("plaus.max", fira_session_range_plausible(FIRA_RANGE_MAX_CM));
	T_OK("implaus.big_neg", !fira_session_range_plausible(-400));
	T_OK("implaus.below_tol", !fira_session_range_plausible(-FIRA_RANGE_NEG_TOL_CM - 1));
	T_OK("implaus.over_max", !fira_session_range_plausible(FIRA_RANGE_MAX_CM + 1));

	t_group("layer 1: Ghost-Peak negative is dropped, not clamped to 0");
	fira_session_set_ccc_range_cm(120, 20u); /* a good close read */
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("good.latched", cm, 120);
	/* The reported exploit: a hugely negative ToF must NOT latch as a valid 0 cm. */
	fira_session_set_ccc_range_cm(-400, 21u);
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("exploit.not_zero", cm, 120);   /* kept last good; did NOT become 0 */
	T_EQ("exploit.block_kept", block, 20u); /* the spoof block never latched */
	/* An over-envelope outlier is likewise dropped. */
	fira_session_set_ccc_range_cm(FIRA_RANGE_MAX_CM + 500, 22u);
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("overmax.dropped", cm, 120);
	/* Small negative is legit point-blank slop and still reads as 0. */
	fira_session_set_ccc_range_cm(-3, 23u);
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("small_neg.zero", cm, 0);

	t_group("layer 2: STS-quality floor");
	T_OK("sts.good", fira_session_sts_quality_ok(0, 100));
	T_OK("sts.good_at_floor", fira_session_sts_quality_ok(5, FIRA_STS_QUALITY_MIN));
	T_OK("sts.bad_verdict", !fira_session_sts_quality_ok(-1, 100));
	T_OK("sts.below_floor",
	     !fira_session_sts_quality_ok(0, (int16_t)(FIRA_STS_QUALITY_MIN - 1)));

	t_group("layer 4: cross-block consensus builds and breaks trust");
	fira_session_set_ccc_range_cm(90, 30u);
	fira_session_set_ccc_range_cm(92, 31u); /* within SPREAD of 90 */
	T_OK("trust.building", !fira_session_range_trusted()); /* only 2 agreeing */
	fira_session_set_ccc_range_cm(91, 32u); /* 3rd agreeing -> K reached */
	T_OK("trust.reached", fira_session_range_trusted());
	/* A lone plausible outlier breaks trust but still latches (live telemetry). */
	fira_session_set_ccc_range_cm(300, 33u); /* jump > SPREAD */
	fira_session_last_range(&cm, NULL, NULL, NULL, NULL);
	T_EQ("outlier.latched", cm, 300);
	T_OK("outlier.untrusted", !fira_session_range_trusted());
	/* Rebuild trust, then a spoof (implausible) block clears it and does not latch. */
	fira_session_set_ccc_range_cm(305, 34u);
	fira_session_set_ccc_range_cm(307, 35u);
	fira_session_set_ccc_range_cm(303, 36u);
	T_OK("trust.rebuilt", fira_session_range_trusted());
	fira_session_set_ccc_range_cm(-500, 37u); /* Ghost-Peak */
	T_OK("spoof.clears_trust", !fira_session_range_trusted());
	fira_session_last_range(&cm, NULL, NULL, &block, NULL);
	T_EQ("spoof.range_kept", cm, 303);
	T_EQ("spoof.block_kept", block, 36u);

	t_group("layer 4: trust saturates at K and an outlier forces a full rebuild");
	fira_session_set_ccc_range_cm(200, 40u);
	fira_session_set_ccc_range_cm(202, 41u);
	fira_session_set_ccc_range_cm(201, 42u); /* K reached */
	fira_session_set_ccc_range_cm(203, 43u); /* extra agreeing: trust caps at K */
	fira_session_set_ccc_range_cm(199, 44u);
	T_OK("sat.trusted", fira_session_range_trusted());
	/* One jump > SPREAD must drop trust to a single fresh sample, so a lone
	 * agreeing block after it is NOT enough — a full K-run has to rebuild. */
	fira_session_set_ccc_range_cm(400, 45u); /* jump -> run restarts at 1 */
	T_OK("sat.jump.untrusted", !fira_session_range_trusted());
	fira_session_set_ccc_range_cm(402, 46u); /* 2 */
	T_OK("sat.one_after_jump_insufficient", !fira_session_range_trusted());
	fira_session_set_ccc_range_cm(401, 47u); /* 3 -> K reached again */
	T_OK("sat.full_rebuild_trusted", fira_session_range_trusted());

	t_group("layer 2: evidence recorded with the range");
	struct fira_range_integrity ig;

	fira_session_reset_ranges();
	T_OK("ig.none_before_a_range", !fira_session_last_range_integrity(&ig));
	/* Latches with no preceding evidence call must read as a failed STS. This is
	 * the fail-closed default the whole design rests on: a producer that says
	 * nothing about correlation has not said it was good. Run a full K of them,
	 * so the verdict is being denied by the missing evidence and not merely by
	 * a consensus that has not built up yet. */
	for (unsigned i = 0; i < FIRA_RANGE_TRUST_K; i++) {
		fira_session_set_ccc_range_cm(100 + (int32_t)i, 60u + i);
	}
	T_OK("ig.latched", fira_session_last_range_integrity(&ig));
	T_OK("ig.trust_built_without_evidence", fira_session_range_trusted());
	T_OK("ig.no_evidence_is_not_ok", !ig.sts_ok);
	/* K good blocks earn the verdict, and not one block sooner: the evidence
	 * window is the same K that layer 4 uses for trust. */
	fira_session_reset_ranges();
	fira_session_set_ccc_range_sts(0, 200);
	fira_session_set_ccc_range_cm(100, 61u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.one_good_block_insufficient", !ig.sts_ok);
	fira_session_set_ccc_range_sts(0, 150);
	fira_session_set_ccc_range_cm(101, 62u);
	fira_session_set_ccc_range_sts(0, 180);
	fira_session_set_ccc_range_cm(102, 63u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.k_good_blocks_ok", ig.sts_ok);
	T_EQ("ig.quality_is_the_worst_in_the_run", ig.sts_quality, 150);
	T_EQ("ig.trust_level_reported", ig.trust_level, FIRA_RANGE_TRUST_K);
	/* One suspect block drops the verdict even though the distances still agree
	 * and layer 4 trust is untouched. Without this, an attacker only has to land
	 * a single spoofed block inside an otherwise clean run. */
	fira_session_set_ccc_range_sts(-1, 200);
	fira_session_set_ccc_range_cm(103, 64u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.one_suspect_block_clears_it", !ig.sts_ok);
	T_OK("ig.layer4_trust_survives_it", fira_session_range_trusted());
	/* ...and it heals after K clean blocks, rather than poisoning a stationary
	 * phone until the distance happens to jump. */
	fira_session_set_ccc_range_sts(0, 210);
	fira_session_set_ccc_range_cm(104, 65u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.heal.not_yet", !ig.sts_ok);
	fira_session_set_ccc_range_sts(0, 220);
	fira_session_set_ccc_range_cm(105, 66u);
	fira_session_set_ccc_range_sts(0, 230);
	fira_session_set_ccc_range_cm(106, 67u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.heal.after_k_clean_blocks", ig.sts_ok);
	T_EQ("ig.heal.quality_forgets_the_bad_run", ig.sts_quality, 210);
	/* Evidence is consumed, never inherited: a second latch with no fresh call
	 * must not reuse the previous block's good verdict. */
	fira_session_reset_ranges();
	fira_session_set_ccc_range_sts(0, 200);
	fira_session_set_ccc_range_cm(110, 70u);
	fira_session_set_ccc_range_cm(111, 71u);
	fira_session_set_ccc_range_cm(112, 72u);
	fira_session_last_range_integrity(&ig);
	T_OK("ig.not_inherited", !ig.sts_ok);
	/* An implausible block clears the evidence as well as the trust. */
	fira_session_reset_ranges();
	for (unsigned i = 0; i < FIRA_RANGE_TRUST_K; i++) {
		fira_session_set_ccc_range_sts(0, 200);
		fira_session_set_ccc_range_cm(120 + (int32_t)i, 80u + i);
	}
	fira_session_last_range_integrity(&ig);
	T_OK("ig.clean_run_ok", ig.sts_ok);
	fira_session_set_ccc_range_sts(0, 200);
	fira_session_set_ccc_range_cm(-500, 90u); /* Ghost-Peak: rejected by layer 1 */
	fira_session_last_range_integrity(&ig);
	T_OK("ig.implausible_clears_evidence", !ig.sts_ok);

	t_group("range listener fires on accepted latches only");
	s_listener_hits = 0;
	fira_session_set_range_listener(range_listener);
	fira_session_set_ccc_range_cm(100, 50u); /* accepted */
	T_EQ("listener.accepted", s_listener_hits, 1);
	fira_session_set_ccc_range_cm(-500, 51u); /* rejected (implausible) */
	T_EQ("listener.rejected_silent", s_listener_hits, 1);
	fira_session_set_ccc_range_cm(102, 52u); /* accepted again */
	T_EQ("listener.accepted_again", s_listener_hits, 2);
	fira_session_set_range_listener(NULL); /* cleared: latches stay silent */
	fira_session_set_ccc_range_cm(103, 53u);
	T_EQ("listener.cleared", s_listener_hits, 2);
}
