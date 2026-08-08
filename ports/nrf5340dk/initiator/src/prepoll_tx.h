// Device-side CCC Pre-POLL transmitter: turns the URSK this board just agreed
// over BLE into the SP0 data frame that opens a ranging block, and puts it on
// the air on the session's channel.
/*
 * The first piece of the UWB *initiator* role. The reader half already exists
 * and is the mirror of this: modules/woz_uwb/src/ccc/ccc_shim_rx.c decodes a
 * Pre-POLL in prepoll_decode(), which is the function this file is written to
 * satisfy.
 *
 * SCOPE, stated plainly: Pre-POLL only. A ranging block is Pre-POLL, POLL,
 * RESPONSE, Final, Final_Data; this sends the first of the five and no more, so
 * no distance is measured. What it proves is that the two boards agree on the
 * SP0 key schedule and the UWB addresses, which is the gate every later frame
 * sits behind.
 *
 * Why Pre-POLL alone is enough to prove that: the Pre-POLL's AES-CCM* MIC is
 * keyed by mUPSK1 (URSK alone) over a nonce built from SrcLongAddr (URSK +
 * STS_Index0). The reader recomputes both and drops the frame silently on a
 * mismatch (ccc_shim_rx.c, ccc_sp0_decrypt -> return). So "PREPOLL OK" on the
 * reader is not a receive-any-frame result; it is proof of a shared key.
 *
 * Not synchronised to the reader's block clock. It does not need to be: the
 * reader's Pre-POLL listener self-rearms after every RX outcome and so is
 * always listening (ccc_prepoll_listen). Slot-timed transmission only becomes
 * necessary at the POLL, which must land in a window the reader arms.
 */
#pragma once

#include <stdint.h>

/** The ranging-setup values a Pre-POLL needs, gathered across M1, M3 and M4. */
struct prepoll_tx_params {
	const uint8_t *ursk;      /**< 32 bytes, the key the Access Protocol produced. */
	uint32_t uwb_session_id;  /**< M1: UWB Session Identifier. */
	uint32_t sts_index0;      /**< M4: STS_Index0 (this board chose it). */
	uint32_t hop_key_rw;      /**< M4: Hop Mode Key. */
	uint8_t n_ran_s;          /**< M2: the RAN multiplier this board selected. */
	uint8_t n_chap_per_slot;  /**< M3: N_Chap_per_Slot. */
	uint8_t n_responder;      /**< M3: N_Responder. */
	uint8_t n_slot_per_round; /**< M3: N_Slot_per_Round. */
	uint8_t channel;          /**< M2: the UWB channel this board selected. */
	uint8_t sync_code_index;  /**< M4: the preamble code this board selected. */
};

/**
 * Derive the SP0 key material, configure the radio for SP0 and start sending one
 * Pre-POLL per ranging block. Idempotent: a second call restarts the schedule.
 *
 * Returns 0 once the first frame is scheduled, negative on a bad parameter set
 * or a radio that will not configure. It does NOT wait for the reader.
 */
int initiator_prepoll_tx_start(const struct prepoll_tx_params *p);

/** Stop transmitting and force the radio off. Safe if never started. */
void initiator_prepoll_tx_stop(void);
