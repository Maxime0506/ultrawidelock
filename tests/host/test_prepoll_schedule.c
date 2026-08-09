/** @file test_prepoll_schedule.c — the ranging schedule the DK initiator publishes.
 *
 * The initiator's Pre-POLL announces a Poll_STS_Index, and the reader does not
 * compute one: it reads what it is told (ccc_shim_rx.c, prepoll_decode) and
 * learns the per-block stride by subtracting consecutive announcements. So the
 * schedule the initiator computes is load-bearing for the reader in a way the
 * reader itself cannot check, and its properties are worth pinning here.
 *
 * examples/zephyr/nrf5340dk-initiator/src/prepoll_tx.c is not host-buildable -- it is
 * Zephyr work queues and dwt_* calls -- so this exercises the library calls it
 * makes, with the M3 parameters the bench reader actually sends, rather than
 * the file. The frame construction those indices go into is already covered
 * end-to-end against the real listener by test_prepoll_round.c.
 */
#include <errno.h>
#include <stdbool.h>

#include "ccc_mac.h"
#include "ccc_session.h"
#include "test.h"

/* The values the CDK reader sent in M3 on 2026-08-07, plus the two the device
 * chooses in M2/M4. Hard-coded on purpose: if a reader change moves them, this
 * test should be the thing that notices. */
#define M3_RAN_MULT        4u
#define M3_CHAPS_PER_SLOT  3u
#define M3_RESPONDERS      1u
#define M3_SLOTS_PER_ROUND 12u
#define M4_STS_INDEX0      0x1000u
#define M4_HOP_KEY         0x11223344u

/* 288 * 4 / (3 * 12) = 32. */
#define EXPECT_N_ROUND 32u
/* One block advances the index by N_Slot_per_Round * N_Round = 12 * 32. */
#define EXPECT_STRIDE  384u

static struct ccc_ran_session bench(enum ccc_hop_mode hop)
{
	struct ccc_ran_session s = {
		.uwb_session_id = 0x0badf00du,
		.sts_index0 = M4_STS_INDEX0,
		.uwb_time0 = 0u,
		.hop_key_rw = M4_HOP_KEY,
		.mac_mode_offset = 0u,
		.n_ran_s = M3_RAN_MULT,
		.n_chap_per_slot = M3_CHAPS_PER_SLOT,
		.n_responder = M3_RESPONDERS,
		.n_slot_per_round = M3_SLOTS_PER_ROUND,
		.hop_mode = hop,
	};
	return s;
}

void test_prepoll_schedule(void)
{
	struct ccc_ran_session s = bench(CCC_HOP_NONE);
	struct ccc_ran_params p;
	uint32_t prev, stride;

	t_group("bench M3 forms a schedule");
	/* The initiator refuses to transmit if this fails, which is the check that
	 * turns bad M3 arithmetic into a log line instead of dead air. */
	T_EQ("map.ok", ccc_session_to_ran_params(&s, &p), 0);
	T_EQ("map.n_round", p.n_round, EXPECT_N_ROUND);
	T_EQ("map.n_slot", p.n_slot_per_round, M3_SLOTS_PER_ROUND);
	T_EQ("map.sts0", p.sts_index0, M4_STS_INDEX0);

	t_group("the round holds every packet of a block");
	/* Pre-POLL, POLL, N responses, Final, Final_Data. The mapping rejects a
	 * round shorter than N_Responder + 4; assert the offsets really do land
	 * inside the round, which is the property that rule is protecting. */
	T_EQ("slot.prepoll", ccc_slot_sts_index(&p, 0u, 0u, CCC_SLOT_PRE_POLL, 0u), M4_STS_INDEX0);
	T_EQ("slot.poll", ccc_slot_sts_index(&p, 0u, 0u, CCC_SLOT_POLL, 0u), M4_STS_INDEX0 + 1u);
	T_EQ("slot.resp0", ccc_slot_sts_index(&p, 0u, 0u, CCC_SLOT_RESPONSE, 0u),
	     M4_STS_INDEX0 + 2u);
	/* Final sits one slot past the last responder: POLL + N + 1, which is what
	 * ALIRO_FINAL_SLOT_OFFSET encodes on the reader side. */
	T_EQ("slot.final", ccc_slot_sts_index(&p, 0u, 0u, CCC_SLOT_FINAL, 0u),
	     M4_STS_INDEX0 + M3_RESPONDERS + 2u);
	T_EQ("slot.final_data", ccc_slot_sts_index(&p, 0u, 0u, CCC_SLOT_FINAL_DATA, 0u),
	     M4_STS_INDEX0 + M3_RESPONDERS + 3u);
	T_OK("slots.fit_round", (uint32_t)M3_RESPONDERS + 3u < M3_SLOTS_PER_ROUND);

	t_group("no hopping: every block uses round 0");
	{
		bool all_zero = true;

		for (uint32_t b = 0u; b < 64u; b++) {
			if (ccc_block_round(&p, b) != 0u) {
				all_zero = false;
				break;
			}
		}
		T_OK("hop_none.round0", all_zero);
	}

	t_group("the stride the reader learns is constant");
	/*
	 * prepoll_decode() does g_poll_stride = pp.poll_sts_index - g_poll_sts_index
	 * on consecutive Pre-POLLs, then pre-warms the NEXT block's STS at
	 * index + stride. A schedule whose stride varies would warm the wrong keys
	 * and every POLL after the first would miss, with nothing in either log
	 * saying why. This is why prepoll_tx.c pins hop_mode to CCC_HOP_NONE.
	 */
	prev = ccc_slot_sts_index(&p, 0u, ccc_block_round(&p, 0u), CCC_SLOT_POLL, 0u);
	T_EQ("stride.block0", prev, M4_STS_INDEX0 + 1u);
	stride = EXPECT_STRIDE;
	for (uint32_t b = 1u; b < 64u; b++) {
		uint32_t idx = ccc_slot_sts_index(&p, b, ccc_block_round(&p, b), CCC_SLOT_POLL, 0u);

		stride = idx - prev;
		if (stride != EXPECT_STRIDE) {
			break; /* report the offending value once, below */
		}
		prev = idx;
	}
	T_EQ("stride.constant", stride, EXPECT_STRIDE);

	t_group("continuous hopping would NOT give a constant stride");
	/* The reason the initiator cannot simply turn hopping on. Recorded as a
	 * test so enabling it is a deliberate act with a failing assertion to
	 * answer, not a one-line edit that silently breaks the pre-warm. */
	{
		struct ccc_ran_session h = bench(CCC_HOP_CONTINUOUS);
		struct ccc_ran_params hp;
		bool varies = false;

		T_EQ("hop.map.ok", ccc_session_to_ran_params(&h, &hp), 0);
		prev = ccc_slot_sts_index(&hp, 0u, ccc_block_round(&hp, 0u), CCC_SLOT_POLL, 0u);
		for (uint32_t b = 1u; b < 64u; b++) {
			uint32_t idx = ccc_slot_sts_index(&hp, b, ccc_block_round(&hp, b),
							  CCC_SLOT_POLL, 0u);

			if (idx - prev != EXPECT_STRIDE) {
				varies = true;
				break;
			}
			prev = idx;
		}
		T_OK("hop.stride_varies", varies);
	}

	t_group("the URSK outlives a bench run");
	/* Pre-POLLs go out every 200 ms, so 64 blocks is ~13 s and 2^31 indices is
	 * far away. Assert the lifetime check agrees rather than assuming it. */
	T_OK("ursk.block0", !ccc_ursk_exhausted(&p, 0u));
	T_OK("ursk.block_1e6", !ccc_ursk_exhausted(&p, 1000000u));

	t_group("bad M3 stops the transmitter");
	{
		struct ccc_ran_session tight = bench(CCC_HOP_NONE);

		tight.n_slot_per_round = M3_RESPONDERS + 3u; /* one slot short */
		T_EQ("bad.slots_short", ccc_session_to_ran_params(&tight, &p), -EINVAL);
	}
	{
		struct ccc_ran_session slow = bench(CCC_HOP_NONE);

		/* A round so long that 288 * N_RAN_S / (chaps * slots) floors to zero:
		 * no whole round fits a block, so there is no schedule to publish. */
		slow.n_slot_per_round = 200u;
		slow.n_chap_per_slot = 100u;
		T_EQ("bad.zero_round", ccc_session_to_ran_params(&slow, &p), -EINVAL);
	}
}
