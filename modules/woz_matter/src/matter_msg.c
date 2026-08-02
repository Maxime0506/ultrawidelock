/**
 * @file matter_msg.c — Matter message and protocol header codec.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Both decoders compute the full header length BEFORE reading any optional
 * field, so a truncated datagram is refused by one comparison rather than
 * discovered halfway through a parse. That ordering is the whole safety story
 * here: the optional fields are selected by attacker-controlled flag bits, so
 * "check as you go" would mean the attacker picks how far the read walks.
 */
#include <string.h>

#include "matter_msg.h"

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
	return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static void wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
	wr32(p, (uint32_t)v);
	wr32(p + 4, (uint32_t)(v >> 32));
}

/**
 * Header length implied by a message flags byte, or 0 if the byte is invalid.
 *
 * Rejecting DSIZ 3 here rather than treating it as "no destination" matters: it
 * is reserved, so a peer using it is either broken or probing, and silently
 * accepting would put this decoder's idea of the payload offset out of step
 * with the sender's.
 */
static size_t msg_header_len(uint8_t flags)
{
	size_t n = MATTER_MSG_HEADER_MIN;

	if ((flags & MATTER_MSG_VERSION_MASK) != 0u) {
		return 0u;
	}
	if ((flags & MATTER_MSG_FLAG_S) != 0u) {
		n += 8u;
	}
	switch (flags & MATTER_MSG_DSIZ_MASK) {
	case MATTER_MSG_DSIZ_NONE:
		break;
	case MATTER_MSG_DSIZ_NODE:
		n += 8u;
		break;
	case MATTER_MSG_DSIZ_GROUP:
		n += 2u;
		break;
	default:
		return 0u;
	}
	return n;
}

static size_t proto_header_len(uint8_t exchange_flags)
{
	size_t n = MATTER_PROTO_HEADER_MIN;

	if ((exchange_flags & MATTER_EX_FLAG_V) != 0u) {
		n += 2u;
	}
	if ((exchange_flags & MATTER_EX_FLAG_A) != 0u) {
		n += 4u;
	}
	return n;
}

bool matter_msg_is_secure(const struct matter_msg_header *h)
{
	if (h == NULL) {
		return false;
	}
	/* Session 0 is the unsecured unicast session (MessageHeader.h:74). A group
	 * session is always secured regardless of the ID. */
	if ((h->security_flags & MATTER_SEC_SESSION_TYPE_MASK) == MATTER_SESSION_TYPE_GROUP) {
		return true;
	}
	return h->session_id != 0u;
}

int matter_msg_header_decode(const uint8_t *buf, size_t len, struct matter_msg_header *h,
			     size_t *consumed)
{
	size_t need;
	size_t off;

	if (buf == NULL || h == NULL) {
		return MATTER_E_INVAL;
	}
	if (len < MATTER_MSG_HEADER_MIN) {
		return MATTER_E_TRUNC;
	}

	memset(h, 0, sizeof(*h));
	h->flags = buf[0];

	need = msg_header_len(h->flags);
	if (need == 0u) {
		return MATTER_E_INVAL;
	}
	if (len < need) {
		return MATTER_E_TRUNC;
	}

	h->session_id = rd16(&buf[1]);
	h->security_flags = buf[3];
	h->message_counter = rd32(&buf[4]);

	off = MATTER_MSG_HEADER_MIN;
	if ((h->flags & MATTER_MSG_FLAG_S) != 0u) {
		h->source_node_id = rd64(&buf[off]);
		off += 8u;
	}
	switch (h->flags & MATTER_MSG_DSIZ_MASK) {
	case MATTER_MSG_DSIZ_NODE:
		h->dest_node_id = rd64(&buf[off]);
		off += 8u;
		break;
	case MATTER_MSG_DSIZ_GROUP:
		h->dest_group_id = rd16(&buf[off]);
		off += 2u;
		break;
	default:
		break;
	}

	if (consumed != NULL) {
		*consumed = off;
	}
	return MATTER_OK;
}

int matter_msg_header_encode(const struct matter_msg_header *h, uint8_t *buf, size_t cap,
			     size_t *written)
{
	size_t need;
	size_t off;

	if (h == NULL || buf == NULL) {
		return MATTER_E_INVAL;
	}

	need = msg_header_len(h->flags);
	if (need == 0u) {
		return MATTER_E_INVAL;
	}
	if (cap < need) {
		return MATTER_E_NOSPACE;
	}

	buf[0] = h->flags;
	wr16(&buf[1], h->session_id);
	buf[3] = h->security_flags;
	wr32(&buf[4], h->message_counter);

	off = MATTER_MSG_HEADER_MIN;
	if ((h->flags & MATTER_MSG_FLAG_S) != 0u) {
		wr64(&buf[off], h->source_node_id);
		off += 8u;
	}
	switch (h->flags & MATTER_MSG_DSIZ_MASK) {
	case MATTER_MSG_DSIZ_NODE:
		wr64(&buf[off], h->dest_node_id);
		off += 8u;
		break;
	case MATTER_MSG_DSIZ_GROUP:
		wr16(&buf[off], h->dest_group_id);
		off += 2u;
		break;
	default:
		break;
	}

	if (written != NULL) {
		*written = off;
	}
	return MATTER_OK;
}

void matter_counter_init(struct matter_counter *c, uint32_t entropy, enum matter_counter_kind kind)
{
	if (c == NULL) {
		return;
	}
	/* Store the PREDECESSOR, so the first value handed out is one higher and
	 * therefore never 0 -- a peer starts its idea of our counter at 0, and the
	 * first message has to be greater than that. */
	c->last_used = entropy & MATTER_COUNTER_INIT_MASK;
	c->kind = (uint8_t)kind;
}

int matter_counter_next(struct matter_counter *c, uint32_t *out)
{
	if (c == NULL || out == NULL) {
		return MATTER_E_INVAL;
	}
	/* Wrapping a secure session's counter would repeat an AEAD nonce under a
	 * key still in use, so it stops instead. The unsecured counter has no key
	 * to protect and wraps, which is what CHIP does for each
	 * (MessageCounter.h:68-72 vs :93-102). */
	if (c->kind == (uint8_t)MATTER_COUNTER_SESSION && c->last_used == UINT32_MAX) {
		return MATTER_E_STATE;
	}
	c->last_used++;
	*out = c->last_used;
	return MATTER_OK;
}

int matter_proto_header_decode(const uint8_t *buf, size_t len, struct matter_proto_header *h,
			       size_t *consumed)
{
	size_t need;
	size_t off;

	if (buf == NULL || h == NULL) {
		return MATTER_E_INVAL;
	}
	if (len < MATTER_PROTO_HEADER_MIN) {
		return MATTER_E_TRUNC;
	}

	memset(h, 0, sizeof(*h));
	h->exchange_flags = buf[0];

	need = proto_header_len(h->exchange_flags);
	if (len < need) {
		return MATTER_E_TRUNC;
	}

	h->opcode = buf[1];
	h->exchange_id = rd16(&buf[2]);

	off = 4u;
	if ((h->exchange_flags & MATTER_EX_FLAG_V) != 0u) {
		h->vendor_id = rd16(&buf[off]);
		off += 2u;
	}
	h->protocol_id = rd16(&buf[off]);
	off += 2u;
	if ((h->exchange_flags & MATTER_EX_FLAG_A) != 0u) {
		h->ack_counter = rd32(&buf[off]);
		off += 4u;
	}

	if (consumed != NULL) {
		*consumed = off;
	}
	return MATTER_OK;
}

int matter_proto_header_encode(const struct matter_proto_header *h, uint8_t *buf, size_t cap,
			       size_t *written)
{
	size_t need;
	size_t off;

	if (h == NULL || buf == NULL) {
		return MATTER_E_INVAL;
	}

	need = proto_header_len(h->exchange_flags);
	if (cap < need) {
		return MATTER_E_NOSPACE;
	}

	buf[0] = h->exchange_flags;
	buf[1] = h->opcode;
	wr16(&buf[2], h->exchange_id);

	off = 4u;
	if ((h->exchange_flags & MATTER_EX_FLAG_V) != 0u) {
		wr16(&buf[off], h->vendor_id);
		off += 2u;
	}
	wr16(&buf[off], h->protocol_id);
	off += 2u;
	if ((h->exchange_flags & MATTER_EX_FLAG_A) != 0u) {
		wr32(&buf[off], h->ack_counter);
		off += 4u;
	}

	if (written != NULL) {
		*written = off;
	}
	return MATTER_OK;
}
