/** @file twin_frames.h — the digital twin's peer (iPhone) side, shared.
 *
 * Builds genuinely CCM*-encrypted CCC frames (Pre-POLL, Final_Data) and feeds
 * them to the real responder through the injectable host radio (woz_host_rx).
 * Two consumers, one implementation: tests/host/test_twin.c (the walk-up twin
 * suite) and web-twin/twin_glue.c (the same scenario compiled to WASM for the
 * interactive twin page). Keeping the peer model here means the page and the
 * test can never disagree about how a round is driven.
 */
#ifndef WOZ_HOST_TWIN_FRAMES_H
#define WOZ_HOST_TWIN_FRAMES_H

#include <stdint.h>

#include <deca_device_api.h>

#include "ccc_kdf.h"

/** Per-session peer constants, derived from the URSK exactly as prepoll_decode
 * does on the reader side (mUPSK1/mUPSK2 -> UAD -> addresses). */
struct twin_peer {
	uint32_t sid;                              /**< UWB session id on the wire. */
	uint8_t mupsk1[CCC_MUPSK1_LEN];            /**< Pre-POLL SP0 key. */
	uint8_t ks[CCC_KEYSOURCE_LEN];             /**< MHR KeySource. */
	uint8_t dest[CCC_DEST_SHORT_ADDR_LEN];     /**< MHR dest short address. */
	uint8_t src_long[CCC_SRC_LONG_ADDR_LEN];   /**< CCM* nonce source address. */
};

/** Derive the peer's session constants from @p ursk + @p sts_index0. */
void twin_peer_init(struct twin_peer *p, const uint8_t *ursk, uint32_t sid, uint32_t sts_index0);

/** Build an encrypted Pre-POLL frame; returns its on-air length. */
uint16_t twin_mk_prepoll(const struct twin_peer *p, uint8_t *out, uint32_t fc, uint32_t poll_idx,
			 uint32_t block);

/** Build an encrypted Final_Data (1 responder) keyed on the armed POLL index;
 * @p t_round1 / @p t_reply2 are the initiator-side DS-TWR intervals (DTU). */
uint16_t twin_mk_final_data(const struct twin_peer *p, uint8_t *out, uint32_t fc,
			    uint32_t armed_idx, uint32_t block, uint32_t t_round1,
			    uint32_t t_reply2);

/** Load a frame + Ipatov timestamp into the host radio stub. */
void twin_stash_frame(const uint8_t *frame, uint16_t len, uint64_t ip40);

/** Fire a captured RX/TX callback the way dwt_isr would. */
void twin_rx_event(dwt_cb_t cb, uint32_t status);

/* Good-frame status: CIA done (timestamp valid) + PHR + CRC good. */
#define TWIN_ST_GOOD (DWT_INT_CIADONE_BIT_MASK | DWT_INT_RXPHD_BIT_MASK | DWT_INT_RXFCG_BIT_MASK)

#endif /* WOZ_HOST_TWIN_FRAMES_H */
