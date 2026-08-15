/* SPDX-License-Identifier: ISC */

// Device-side CCC Pre-POLL transmitter: turns the URSK this board just agreed
// over BLE into the SP0 data frame that opens a ranging block, and puts it on
// the air on the session's channel.
/*
 * The first piece of the UWB *initiator* role, written to satisfy the reader's
 * prepoll_decode() (ccc_shim_rx.c). Pre-POLL only -- the first of a block's
 * five frames, so no distance is measured; what it proves is a shared SP0 key
 * schedule, because the Pre-POLL's AES-CCM* MIC is keyed by mUPSK1 over a
 * nonce built from SrcLongAddr, both derived from the URSK, and the reader
 * silently drops a mismatch. "PREPOLL OK" is proof of a shared key. No sync to
 * the reader's block clock needed: its listener self-rearms and is always
 * listening; slot timing only matters from the POLL on.
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
