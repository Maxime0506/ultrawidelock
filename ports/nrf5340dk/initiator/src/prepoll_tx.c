// Device-side CCC Pre-POLL transmitter. See prepoll_tx.h for scope.
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "prepoll_tx.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <deca_device_api.h>

#include "ccc_kdf.h"
#include "ccc_mac.h"
#include "ccc_session.h"
#include "uwb_min.h"
#include "uwb_seam.h"

LOG_MODULE_REGISTER(prepoll_tx, LOG_LEVEL_INF);

/* How often a Pre-POLL goes out. This is NOT the negotiated CCC ranging-block
 * period -- nothing in the M1-M4 exchange this board runs reports one in
 * milliseconds, and inventing a figure would imply a synchronisation that does
 * not exist yet. It is a bench cadence, and it is sound only because the
 * reader's Pre-POLL listener self-rearms after every RX outcome and is
 * therefore always up. The moment a POLL follows, this constant stops being
 * arbitrary and has to come from the negotiation. */
#define PREPOLL_TX_PERIOD_MS 200

/* SYS_STATUS bits written back after a transmit, so the next poll starts clean. */
#define PREPOLL_TX_STATUS_CLEAR                                                                    \
	(DWT_INT_TXFRB_BIT_MASK | DWT_INT_TXPRS_BIT_MASK | DWT_INT_TXPHS_BIT_MASK |                \
	 DWT_INT_TXFRS_BIT_MASK)

/* MHR + ciphertext + MIC. 44 bytes, and the reader rejects anything shorter
 * (ccc_shim_rx.c: datalength < CCC_MHR_LEN + CCC_PRE_POLL_LEN + CCC_SP0_MIC_LEN). */
#define PREPOLL_FRAME_LEN (CCC_MHR_LEN + CCC_PRE_POLL_LEN + CCC_SP0_MIC_LEN)

/* Per-session constants: derived once at start, reused for every block. Same
 * split the reader makes in prepoll_decode()'s g_uad_cached branch, for the same
 * reason -- these depend only on the URSK and STS_Index0. */
static uint8_t s_mupsk1[CCC_MUPSK1_LEN];
static uint8_t s_keysource[CCC_KEYSOURCE_LEN];
static uint8_t s_dest_short[CCC_DEST_SHORT_ADDR_LEN];
static uint8_t s_src_long[CCC_SRC_LONG_ADDR_LEN];

static struct ccc_ran_params s_ran;
static uint32_t s_session_id;
static uint32_t s_block;
static uint32_t s_frame_counter;
static uint32_t s_sent;
static bool s_running;

static void prepoll_tx_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_work, prepoll_tx_work);

/**
 * Apply the SP0 PHY the reader listens on.
 *
 * Every field here is the reader's own prepoll_apply_phy() in
 * modules/woz_uwb/src/ccc/ccc_shim_rx.c, and it has to stay that way: a Pre-POLL
 * sent on a different preamble length, SFD type or data rate is not a frame the
 * reader can hear at all, and the failure looks exactly like a bad key.
 *
 * The two that were learned on hardware rather than read off a spec, per that
 * file's comments: SFD is the ternary 8-symbol IEEE 4a pattern, not 4z (4z
 * SFD-timed-out on every real phone frame), and preamble length is 64 because
 * CCC config 0 pins it there.
 */
static int prepoll_tx_apply_phy(uint8_t channel, uint8_t preamble_code)
{
	dwt_config_t cfg = {
		.chan = channel,
		.txPreambLength = DWT_PLEN_64,
		.rxPAC = DWT_PAC8,
		.txCode = preamble_code,
		.rxCode = preamble_code,
		.sfdType = DWT_SFD_IEEE_4A,
		.dataRate = DWT_BR_6M8,
		.phrMode = DWT_PHRMODE_STD,
		.phrRate = DWT_PHRRATE_STD,
		.sfdTO = 64 + 1,
		.stsMode = DWT_STS_MODE_OFF, /* SP0 -- data frame, PHR + payload, no STS */
		.stsLength = DWT_STS_LEN_64,
		.pdoaMode = DWT_PDOA_M0,
	};
	int rc = uwb_min_radio_init();

	if (rc != 0) {
		LOG_ERR("radio init failed (%d)", rc);
		return rc;
	}
	/* uwb_min_radio_init() leaves the radio on its own baseline (channel 9,
	 * preamble code 11, SP3-ND). That is a different PHY, so reconfigure --
	 * and force TRX off first, because dwt_configure on a live transceiver is
	 * not defined to be safe. */
	dwt_forcetrxoff();
	if (woz_uwb_configure_phy(&cfg) != DWT_SUCCESS) {
		LOG_ERR("dwt_configure failed (ch=%u code=%u)", channel, preamble_code);
		return -EIO;
	}
	return 0;
}

/** Build one block's 44-byte Pre-POLL into @out. */
static int prepoll_tx_build(uint8_t out[PREPOLL_FRAME_LEN], uint32_t *poll_sts_index_out)
{
	struct ccc_mhr_fields mhr = {
		.frame_counter = s_frame_counter,
		.msg_id = CCC_MSG_ID_PRE_POLL,
		.payload_len = CCC_PRE_POLL_LEN,
	};
	struct ccc_pre_poll pp;
	uint8_t plain[CCC_PRE_POLL_LEN];
	uint16_t round;
	int rc;

	round = ccc_block_round(&s_ran, s_block);
	pp.uwb_session_id = s_session_id;
	pp.poll_sts_index = ccc_slot_sts_index(&s_ran, s_block, round, CCC_SLOT_POLL, 0u);
	/* uint32 block index down to the u16 the payload carries; the reader only
	 * uses it as a label, and the STS index above is the value it acts on. */
	pp.ranging_block = (uint16_t)s_block;
	pp.hop_flag = 0u;
	pp.round_index = round;

	/* DestShort and KeySource are the UAD-derived addresses. The reader logs
	 * them against its own copy as a self-check and does NOT reject a mismatch
	 * -- only the MIC decides -- so getting them right here is what makes that
	 * self-check line worth reading. */
	mhr.dest_short_addr =
		(uint16_t)((uint16_t)s_dest_short[0] | ((uint16_t)s_dest_short[1] << 8));
	memcpy(mhr.key_source, s_keysource, sizeof(mhr.key_source));

	rc = ccc_build_mhr(&mhr, out);
	if (rc != 0) {
		return rc;
	}
	rc = ccc_pre_poll_pack(&pp, plain);
	if (rc != 0) {
		return rc;
	}
	/* The MHR is authenticated but not encrypted; the 13-byte payload is both.
	 * Same argument order the reader decrypts with. */
	rc = ccc_sp0_encrypt(s_mupsk1, s_src_long, mhr.frame_counter, out, CCC_MHR_LEN, plain,
			     CCC_PRE_POLL_LEN, &out[CCC_MHR_LEN],
			     &out[CCC_MHR_LEN + CCC_PRE_POLL_LEN]);
	if (rc != 0) {
		return rc;
	}
	*poll_sts_index_out = pp.poll_sts_index;
	return 0;
}

/** Put one Pre-POLL on the air and wait for TXFRS. */
static int prepoll_tx_one(void)
{
	uint8_t frame[PREPOLL_FRAME_LEN];
	uint32_t poll_sts_index = 0u;
	uint32_t status = 0u;
	int64_t deadline;
	int rc;

	rc = prepoll_tx_build(frame, &poll_sts_index);
	if (rc != 0) {
		LOG_ERR("Pre-POLL build failed (%d)", rc);
		return rc;
	}

	if (dwt_writetxdata(PREPOLL_FRAME_LEN, frame, 0) != DWT_SUCCESS) {
		LOG_ERR("dwt_writetxdata failed");
		return -EIO;
	}
	/* +2 for the FCS the chip appends. ranging=0: the Pre-POLL is a data frame
	 * announcing the block, not one of the timestamped RFRAMEs, and the reader
	 * takes no timestamp from it. Unverified against a real phone's PHR, which
	 * this board has no way to read; if a capture ever shows the bit set, this
	 * is the line to change. */
	dwt_writetxfctrl(PREPOLL_FRAME_LEN + 2u, 0, 0);

	if (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
		LOG_ERR("dwt_starttx failed");
		return -EIO;
	}

	deadline = k_uptime_get() + 20;
	while (k_uptime_get() < deadline) {
		status = dwt_readsysstatuslo();
		if (status & DWT_INT_TXFRS_BIT_MASK) {
			break;
		}
	}
	if ((status & DWT_INT_TXFRS_BIT_MASK) == 0u) {
		LOG_ERR("TX did not complete: SYS_STATUS=0x%08x", status);
		dwt_forcetrxoff();
		return -EIO;
	}
	dwt_writesysstatuslo(PREPOLL_TX_STATUS_CLEAR);

	/* First frame gets a full line; after that one line per 25 blocks (~5 s),
	 * so a long bench run stays readable. */
	if (s_sent == 0u || (s_sent % 25u) == 0u) {
		LOG_INF("Pre-POLL #%u sent: block %u, poll_sts_index 0x%08x, fc %u",
			(unsigned)s_sent, (unsigned)s_block, (unsigned)poll_sts_index,
			(unsigned)s_frame_counter);
	}
	s_sent++;
	s_block++;
	s_frame_counter++;
	return 0;
}

static void prepoll_tx_work(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!s_running) {
		return;
	}
	/* A failed transmit is not fatal to the schedule: the radio is left off by
	 * prepoll_tx_one() and the next tick re-enters through dwt_starttx, which
	 * is how a transient SPI glitch recovers without tearing down the session. */
	(void)prepoll_tx_one();
	k_work_schedule(&s_work, K_MSEC(PREPOLL_TX_PERIOD_MS));
}

int initiator_prepoll_tx_start(const struct prepoll_tx_params *p)
{
	struct ccc_ran_session sess;
	uint8_t mupsk2[CCC_MUPSK2_LEN];
	uint8_t uad[CCC_UAD_LEN];
	int rc;

	if (p == NULL || p->ursk == NULL) {
		return -EINVAL;
	}

	memset(&sess, 0, sizeof(sess));
	memcpy(sess.ursk, p->ursk, CCC_URSK_LEN);
	sess.uwb_session_id = p->uwb_session_id;
	sess.sts_index0 = p->sts_index0;
	sess.hop_key_rw = p->hop_key_rw;
	sess.n_ran_s = p->n_ran_s;
	sess.n_chap_per_slot = p->n_chap_per_slot;
	sess.n_responder = p->n_responder;
	sess.n_slot_per_round = p->n_slot_per_round;
	/* CCC_HOP_NONE, deliberately: every block stays in round 0. Hopping is a
	 * schedule the reader would have to follow us into, and it cannot yet --
	 * it learns the POLL index from the Pre-POLL we send rather than computing
	 * one. Turning it on before there is a POLL to hop would be a schedule
	 * neither side is keeping. */
	sess.hop_mode = CCC_HOP_NONE;

	/* This is the first live caller of ccc_session_to_ran_params(); until now
	 * the whole schedule half of the CCC MAC was reachable only from
	 * tests/host. It rejects a round too short to hold N_Responder + 4 packets
	 * and an N_Round of zero, which is exactly the M3 arithmetic worth failing
	 * on before anything reaches the air. */
	rc = ccc_session_to_ran_params(&sess, &s_ran);
	if (rc != 0) {
		LOG_ERR("M3 parameters do not form a ranging schedule: slots/round %u, "
			"responders %u, chaps/slot %u, ran_mult %u",
			p->n_slot_per_round, p->n_responder, p->n_chap_per_slot, p->n_ran_s);
		return rc;
	}

	if (ccc_derive_mupsk1(sess.ursk, s_mupsk1) != 0 ||
	    ccc_derive_mupsk2(sess.ursk, mupsk2) != 0 ||
	    ccc_derive_uad(mupsk2, sess.sts_index0, uad) != 0 ||
	    ccc_uad_addresses(uad, s_keysource, s_dest_short, s_src_long) != 0) {
		LOG_ERR("SP0 key schedule failed");
		return -EIO;
	}

	rc = prepoll_tx_apply_phy(p->channel, p->sync_code_index);
	if (rc != 0) {
		return rc;
	}

	s_session_id = p->uwb_session_id;
	s_block = 0u;
	s_frame_counter = 0u;
	s_sent = 0u;
	s_running = true;

	LOG_INF("Pre-POLL TX up: ch %u code %u, sts0 0x%08x, %u slots/round, %u rounds/block",
		p->channel, p->sync_code_index, (unsigned)s_ran.sts_index0,
		(unsigned)s_ran.n_slot_per_round, (unsigned)s_ran.n_round);
	/* The reader prints its own copy of these two on its first decode. They are
	 * the fastest way to tell a key mismatch from a PHY mismatch: if the reader
	 * never logs a frame at all, it is the PHY; if it logs one and these differ,
	 * it is STS_Index0. */
	LOG_INF("Pre-POLL addr: dest %02x%02x keysrc %02x%02x%02x%02x", s_dest_short[0],
		s_dest_short[1], s_keysource[0], s_keysource[1], s_keysource[2], s_keysource[3]);

	k_work_schedule(&s_work, K_NO_WAIT);
	return 0;
}

void initiator_prepoll_tx_stop(void)
{
	if (!s_running) {
		return;
	}
	s_running = false;
	(void)k_work_cancel_delayable(&s_work);
	dwt_forcetrxoff();
	LOG_INF("Pre-POLL TX stopped after %u frames", (unsigned)s_sent);
}
