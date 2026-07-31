/**
 * @file matter_exchange.h — the unsecured exchange PASE runs on.
 *
 * Between BTP (a byte pipe) and PASE (five messages) sits the part that makes a
 * Matter message a message: which session it belongs to, which exchange, whether
 * it is a duplicate, and whether the peer is owed an acknowledgement.
 *
 *   in    message header | protocol header | payload
 *   out   message header | protocol header | payload
 *
 * This handles exactly one exchange on the UNSECURED session, which is all
 * commissioning needs before PASE finishes: session id 0, no encryption, the
 * peer as initiator and this node as responder. Secure sessions are a different
 * object -- they carry keys and a different counter -- and arrive with CASE.
 *
 * It deliberately does not know what PASE is. It reports the opcode and hands
 * back the payload; the caller decides what to answer. That keeps the framing
 * testable on its own, and means CASE will reuse it rather than fork it.
 *
 * No timers here either. Duplicate suppression and the ack bookkeeping are
 * state, not scheduling; retransmission is matter_mrp.h's, driven by whoever
 * owns a clock.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Stage 3 of internal/cdk-matter-plan.md.
 *
 * Cross-checked against two implementations:
 *   - CHIP, workspace/modules/lib/matter/src/: the unsecured-session rules in
 *     transport/SessionManager.cpp, the exchange/ack handling in
 *     messaging/ExchangeContext.cpp and messaging/ReliableMessageContext.cpp,
 *     and the header layouts already pinned in matter_msg.h.
 *   - CircuitMatter (github.com/adafruit/circuitmatter): the same flow at
 *     circuitmatter/__init__.py, where an unsecured message is recognised by
 *     session id 0 and answered on the same exchange with the I flag cleared.
 *
 * They agree on the shape. The one thing worth stating because it is easy to get
 * backwards: the responder echoes the initiator's exchange id unchanged and
 * clears I, rather than allocating an exchange id of its own.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_crypto.h"
#include "matter_mrp.h"
#include "matter_msg.h"
#include "matter_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Protocol 0x0000, vendor 0x0000. The only protocol this layer accepts. */
#define MATTER_PROTOCOL_SECURE_CHANNEL 0x0000u

/** Session id 0 is the unsecured session, by definition rather than by policy. */
#define MATTER_SESSION_ID_UNSECURED 0x0000u

/**
 * Worst-case bytes this prepends to a payload.
 *
 * Both headers at their largest. Real unsecured commissioning messages are far
 * smaller -- 8 + 6 with an ack, no node ids -- but a caller sizing a buffer
 * should not have to know that.
 */
#define MATTER_EXCHANGE_HEADER_MAX (MATTER_MSG_HEADER_MAX + MATTER_PROTO_HEADER_MAX)

/**
 * Protocol 0x0001. Everything after commissioning's Secure Channel phase.
 */
#define MATTER_PROTOCOL_INTERACTION_MODEL 0x0001u

struct matter_exchange {
	/**
	 * The secure session, once PASE has built one.
	 *
	 * This object deliberately carries the session as well as the exchange,
	 * which Matter separates. That is sound HERE and nowhere wider: this node
	 * accepts one commissioner at a time (CONFIG_BT_MAX_CONN=1, and
	 * claim_conn() refuses a second), so there is never more than one session
	 * or more than one live exchange to track. A node that had to serve two
	 * exchanges at once would have to split them.
	 *
	 * `secure` false means the unsecured session, id 0, no keys.
	 */
	bool secure;
	/** The id the peer must address us with. Announced in PBKDFParamResponse. */
	uint16_t local_session_id;
	/** The id we address the peer with (SessionManager.cpp:264). */
	uint16_t peer_session_id;
	/**
	 * Keys are role-relative. As the responder we DECRYPT with i2r and
	 * ENCRYPT with r2i -- CryptoContext.cpp:77-78,102-103 selects them by
	 * role, and getting them the wrong way round produces a tag failure with
	 * no hint as to why.
	 */
	struct matter_session_keys keys;
	/**
	 * Whether MRP runs on this exchange at all.
	 *
	 * It does NOT over BLE. Matter gates reliable messaging on the transport:
	 * SecureSession.h:161 and UnauthenticatedSessionTable.h:87 both define
	 * AllowsMRP() as "the peer address is UDP", and ExchangeContext.cpp:109-112
	 * only sets the R flag when the session allows MRP. BTP already guarantees
	 * delivery and ordering, so acknowledging on top of it is duplicated work
	 * the peer does not expect.
	 *
	 * Getting this wrong is not cosmetic. A real iPhone sent
	 * PBKDFParamRequest with exchange flags 0x01 -- I only, no R -- and
	 * dropped the link on a reply that came back with R set and no ack.
	 */
	bool mrp;
	/**
	 * The initiator's ephemeral node id, and whether it sent one.
	 *
	 * On an unsecured session the initiator picks an ephemeral node id and
	 * puts it in the source field; the RESPONDER must send it back as the
	 * DESTINATION, which is how the peer matches a reply to its session.
	 * SessionManager.cpp:296-303 makes the asymmetry explicit: initiator sets
	 * source, responder sets destination.
	 *
	 * Omitting it does not produce an error anywhere. A real iPhone simply
	 * ignored our PBKDFParamResponse and sat on "connecting" until it gave up.
	 */
	uint64_t peer_node_id;
	bool have_peer_node_id;
	/** The initiator's exchange id, echoed on every reply. */
	uint16_t exchange_id;
	/** True once a first message has fixed @ref exchange_id. */
	bool open;
	/** Counters this node stamps on what it sends. */
	struct matter_counter counter;
	/** Counters seen from the peer, for duplicate suppression. */
	struct matter_mrp_window window;
	/**
	 * The peer counter this node still owes an acknowledgement for, and
	 * whether it owes one at all. Matter piggybacks the ack on the reply
	 * when there is one, which is the normal case here: every PASE message
	 * except the last is answered immediately.
	 */
	uint32_t ack_counter;
	bool ack_pending;
};

/**
 * PASE sessions have no operational identity, so the AEAD nonce carries node id
 * zero in both directions (SecureSession.h:337, kUndefinedNodeId). CASE brings
 * real node ids and will need them here.
 */
#define MATTER_PASE_NODE_ID 0ULL

/** What a received message turned out to be. */
struct matter_exchange_in {
	uint8_t opcode;
	/** Secure Channel, or the Interaction Model once the session is secure. */
	uint16_t protocol_id;
	/** Points into the caller's buffer; not copied. */
	const uint8_t *payload;
	size_t payload_len;
	/** True when the peer set R and expects an acknowledgement. */
	bool ack_requested;
	/** True when the peer acknowledged something of ours. */
	bool carries_ack;
	uint32_t acked_counter;
};

/**
 * @param entropy a random word seeding the outbound counter; see
 *        matter_counter_init(). Unsecured counters wrap, so this is about not
 *        advertising uptime, not about safety.
 * @param mrp false for BLE, true for UDP. See struct matter_exchange::mrp --
 *        this is a property of the transport, not a preference.
 */
void matter_exchange_init(struct matter_exchange *x, uint32_t entropy, bool mrp);

/**
 * Decode one received message.
 *
 * Everything here arrives from an unauthenticated peer -- the unsecured session
 * is where a commissioner is still a stranger -- so the checks are exhaustive
 * rather than trusting: version, DSIZ, session id, session type, protocol id,
 * and the vendor flag are all refused rather than ignored when wrong.
 *
 * @param pt scratch for the plaintext of an encrypted message; @p in points into
 *        it. Needs @p len bytes. Unused, and may be NULL, while unsecured.
 * @return MATTER_OK; MATTER_E_DUP when the counter has been seen, in which case
 *         the peer must still be acknowledged but @p in must NOT be acted on;
 *         MATTER_E_STATE for a message belonging to a different exchange;
 *         MATTER_E_INVAL for a message this layer will not carry; or whatever
 *         the header decoders returned.
 */
int matter_exchange_recv(struct matter_exchange *x, const uint8_t *msg, size_t len,
			 struct matter_exchange_in *in, uint8_t *pt, size_t pt_cap);

/**
 * Adopt the secure session PASE just established.
 *
 * After this, messages addressed to @p local_id are decrypted and anything on
 * the unsecured session is refused: a peer that has keys has no business
 * talking in the clear.
 *
 * The exchange id is deliberately released here. The commissioner opens a NEW
 * exchange on the secure session -- PASE's exchange is finished -- so holding
 * the old id would refuse the first real message.
 *
 * @param entropy seeds a FRESH counter. Secure sessions must not reuse the
 *        unsecured session's counter space: a repeated counter under the same
 *        key repeats an AEAD nonce.
 */
int matter_exchange_promote(struct matter_exchange *x, uint16_t local_id, uint16_t peer_id,
			    const struct matter_session_keys *keys, uint32_t entropy);

/**
 * Frame a reply on this exchange.
 *
 * With MRP on, carries any outstanding acknowledgement and sets R so the peer
 * acknowledges this in turn. With MRP off -- which is what BLE uses -- neither
 * flag is ever set, because BTP is already reliable.
 *
 * @param out needs MATTER_EXCHANGE_HEADER_MAX + @p payload_len bytes.
 * @return MATTER_OK, MATTER_E_NOSPACE, MATTER_E_STATE if no message has been
 *         received yet, or MATTER_E_INVAL.
 */
int matter_exchange_reply(struct matter_exchange *x, uint8_t opcode, const uint8_t *payload,
			  size_t payload_len, uint8_t *out, size_t cap, size_t *out_len);

/**
 * Frame a bare acknowledgement, for when there is nothing to say yet.
 *
 * Matter's standalone ack: Secure Channel opcode 0x10, empty payload. Needed
 * when a reply cannot be produced inside the peer's retransmission timer, and
 * after the final message of an exchange.
 *
 * @return MATTER_OK; MATTER_E_STATE when nothing is pending, or whenever MRP is
 *         off, since then there is no such thing as an outstanding ack.
 */
int matter_exchange_standalone_ack(struct matter_exchange *x, uint8_t *out, size_t cap,
				   size_t *out_len);

/** Secure Channel MsgType 0x10 (Constants.h). Not a PASE opcode. */
#define MATTER_SC_OP_ACK 0x10u

#ifdef __cplusplus
}
#endif
