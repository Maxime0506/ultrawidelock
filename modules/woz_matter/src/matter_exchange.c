/**
 * @file matter_exchange.c — the unsecured exchange. See matter_exchange.h.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "matter_exchange.h"

#include <string.h>

void matter_exchange_init(struct matter_exchange *x, uint32_t entropy, bool mrp)
{
	memset(x, 0, sizeof(*x));
	x->mrp = mrp;
	matter_counter_init(&x->counter, entropy, MATTER_COUNTER_UNSECURED);
	matter_mrp_window_init(&x->window);
}

/**
 * Everything about a message header that disqualifies it from this layer.
 *
 * Split out because it is a list of refusals rather than a computation, and
 * because every item is a thing an unauthenticated peer chose.
 */
static int check_msg_header(const struct matter_msg_header *h)
{
	/* Only the unsecured session. A non-zero session id means the peer
	 * believes it holds keys with us; it does not, and answering as though
	 * it might is how a downgrade starts. */
	if (h->session_id != MATTER_SESSION_ID_UNSECURED) {
		return MATTER_E_INVAL;
	}
	if ((h->security_flags & MATTER_SEC_SESSION_TYPE_MASK) != MATTER_SESSION_TYPE_UNICAST) {
		return MATTER_E_INVAL;
	}
	/* Privacy and message extensions both describe transformations this node
	 * does not implement. Ignoring either would mean parsing the rest wrong
	 * rather than parsing it strictly. */
	if ((h->security_flags & (MATTER_SEC_FLAG_P | MATTER_SEC_FLAG_MX)) != 0u) {
		return MATTER_E_INVAL;
	}
	return MATTER_OK;
}

int matter_exchange_recv(struct matter_exchange *x, const uint8_t *msg, size_t len,
			 struct matter_exchange_in *in)
{
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	size_t mh_len = 0u;
	size_t ph_len = 0u;
	int rc;

	if (x == NULL || msg == NULL || in == NULL) {
		return MATTER_E_INVAL;
	}
	memset(in, 0, sizeof(*in));

	rc = matter_msg_header_decode(msg, len, &mh, &mh_len);
	if (rc != MATTER_OK) {
		return rc;
	}
	rc = check_msg_header(&mh);
	if (rc != MATTER_OK) {
		return rc;
	}

	rc = matter_proto_header_decode(msg + mh_len, len - mh_len, &ph, &ph_len);
	if (rc != MATTER_OK) {
		return rc;
	}
	if (ph.protocol_id != MATTER_PROTOCOL_SECURE_CHANNEL) {
		return MATTER_E_INVAL;
	}
	/* A vendor-scoped protocol id is a different namespace entirely, so the
	 * protocol_id check above would have compared the wrong thing. */
	if ((ph.exchange_flags & MATTER_EX_FLAG_V) != 0u) {
		return MATTER_E_INVAL;
	}

	/* The peer opens the exchange and this node answers on it. A second
	 * exchange id on the same unsecured session is a second commissioner,
	 * which this node has no room for. */
	if (!x->open) {
		x->exchange_id = ph.exchange_id;
		x->open = true;
	} else if (ph.exchange_id != x->exchange_id) {
		return MATTER_E_STATE;
	}

	/* Keep the initiator's ephemeral node id: every reply has to be addressed
	 * back to it (SessionManager.cpp:301-303). */
	if ((mh.flags & MATTER_MSG_FLAG_S) != 0u) {
		x->peer_node_id = mh.source_node_id;
		x->have_peer_node_id = true;
	}

	in->opcode = ph.opcode;
	in->payload = msg + mh_len + ph_len;
	in->payload_len = len - mh_len - ph_len;
	in->ack_requested = (ph.exchange_flags & MATTER_EX_FLAG_R) != 0u;
	in->carries_ack = (ph.exchange_flags & MATTER_EX_FLAG_A) != 0u;
	in->acked_counter = ph.ack_counter;

	/*
	 * A duplicate still has to be acknowledged -- the peer is retransmitting
	 * precisely because it thinks the last ack was lost -- but its payload
	 * must not be acted on twice. Hence the ack is recorded before the
	 * return, and the caller is told not to use `in`.
	 */
	if (x->mrp && in->ack_requested) {
		x->ack_counter = mh.message_counter;
		x->ack_pending = true;
	}

	rc = matter_mrp_window_check(&x->window, mh.message_counter);
	if (rc != MATTER_OK) {
		return rc;
	}
	/* Unsecured, so there is nothing to authenticate before committing; on a
	 * secure session this would wait until the tag verified. */
	matter_mrp_window_commit(&x->window, mh.message_counter);

	return MATTER_OK;
}

/**
 * Frame one outbound message on this exchange.
 *
 * @param reliable sets R. Everything in commissioning is reliable except a
 *        standalone ack, which would otherwise ask to be acknowledged and never
 *        terminate.
 */
static int frame(struct matter_exchange *x, uint8_t opcode, bool reliable, const uint8_t *payload,
		 size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)
{
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	size_t mh_len = 0u;
	size_t ph_len = 0u;
	uint32_t counter;
	int rc;

	if (x == NULL || out == NULL || out_len == NULL) {
		return MATTER_E_INVAL;
	}
	if (payload_len > 0u && payload == NULL) {
		return MATTER_E_INVAL;
	}
	if (!x->open) {
		return MATTER_E_STATE;
	}

	rc = matter_counter_next(&x->counter, &counter);
	if (rc != MATTER_OK) {
		return rc;
	}

	memset(&mh, 0, sizeof(mh));
	mh.session_id = MATTER_SESSION_ID_UNSECURED;
	mh.security_flags = MATTER_SESSION_TYPE_UNICAST;
	mh.message_counter = counter;
	/*
	 * No SOURCE node id -- this node has no operational identity yet -- but
	 * the DESTINATION is the initiator's ephemeral node id, which is what the
	 * peer matches the reply against. A responder that leaves this out is
	 * talking to nobody in particular and gets ignored.
	 */
	if (x->have_peer_node_id) {
		mh.flags = MATTER_MSG_DSIZ_NODE;
		mh.dest_node_id = x->peer_node_id;
	} else {
		mh.flags = MATTER_MSG_DSIZ_NONE;
	}

	memset(&ph, 0, sizeof(ph));
	/* I stays clear: the peer initiated this exchange and keeps that role
	 * for its lifetime, however many messages each side sends. */
	ph.exchange_flags = 0u;
	/* Both flags are MRP's, and MRP does not run over a transport that is
	 * already reliable (see struct matter_exchange::mrp). */
	if (reliable && x->mrp) {
		ph.exchange_flags |= MATTER_EX_FLAG_R;
	}
	if (x->mrp && x->ack_pending) {
		ph.exchange_flags |= MATTER_EX_FLAG_A;
		ph.ack_counter = x->ack_counter;
	}
	ph.opcode = opcode;
	ph.exchange_id = x->exchange_id;
	ph.protocol_id = MATTER_PROTOCOL_SECURE_CHANNEL;

	rc = matter_msg_header_encode(&mh, out, cap, &mh_len);
	if (rc != MATTER_OK) {
		return rc;
	}
	rc = matter_proto_header_encode(&ph, out + mh_len, cap - mh_len, &ph_len);
	if (rc != MATTER_OK) {
		return rc;
	}
	if (cap - mh_len - ph_len < payload_len) {
		return MATTER_E_NOSPACE;
	}
	if (payload_len > 0u) {
		memcpy(out + mh_len + ph_len, payload, payload_len);
	}

	/* Only now: an ack that was never encoded is an ack still owed. */
	x->ack_pending = false;

	*out_len = mh_len + ph_len + payload_len;
	return MATTER_OK;
}

int matter_exchange_reply(struct matter_exchange *x, uint8_t opcode, const uint8_t *payload,
			  size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)
{
	return frame(x, opcode, true, payload, payload_len, out, cap, out_len);
}

int matter_exchange_standalone_ack(struct matter_exchange *x, uint8_t *out, size_t cap,
				   size_t *out_len)
{
	if (x == NULL) {
		return MATTER_E_INVAL;
	}
	if (!x->mrp || !x->ack_pending) {
		return MATTER_E_STATE;
	}
	return frame(x, MATTER_SC_OP_ACK, false, NULL, 0u, out, cap, out_len);
}
