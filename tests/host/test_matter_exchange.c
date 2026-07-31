/**
 * @file test_matter_exchange.c — the unsecured exchange PASE rides on.
 *
 * Inbound messages are built here with matter_msg.c's own encoders rather than
 * pasted as hex. That is deliberate and not circular: those encoders are pinned
 * bit by bit against CHIP and CircuitMatter in test_matter_msg.c, so using them
 * to construct a peer's message tests THIS layer's decisions -- which messages
 * it refuses, which exchange it binds to, when it owes an acknowledgement --
 * rather than re-testing the header layout underneath it.
 *
 * The refusals matter more than the happy path. Every field checked here was
 * chosen by an unauthenticated stranger: the unsecured session is exactly the
 * window where a commissioner has proved nothing.
 */
#include <string.h>

#include "matter_exchange.h"

#include "test.h"

#define PEER_EXCHANGE_ID 0x1A2Bu
#define SEED		 0x0BADF00Du
/* An initiator's ephemeral node id, as seen on the wire from a real iPhone. */
#define PEER_NODE_ID	 0x6557F7497EA9A507ULL

/** Build a message as a peer would send it. */
static size_t inbound(uint8_t *buf, size_t cap, uint8_t opcode, uint32_t counter,
		      uint16_t exchange_id, uint16_t session_id, uint16_t protocol_id,
		      uint8_t extra_sec_flags, uint8_t extra_ex_flags, const uint8_t *payload,
		      size_t payload_len)
{
	struct matter_msg_header mh;
	struct matter_proto_header ph;
	size_t mh_len = 0u;
	size_t ph_len = 0u;

	memset(&mh, 0, sizeof(mh));
	/* A real commissioner sets S and carries an ephemeral source node id;
	 * the responder has to echo it back as the DESTINATION. */
	mh.flags = MATTER_MSG_DSIZ_NONE | MATTER_MSG_FLAG_S;
	mh.source_node_id = PEER_NODE_ID;
	mh.session_id = session_id;
	mh.security_flags = (uint8_t)(MATTER_SESSION_TYPE_UNICAST | extra_sec_flags);
	mh.message_counter = counter;

	memset(&ph, 0, sizeof(ph));
	/* The peer is the initiator, and asks to be acknowledged. */
	ph.exchange_flags = (uint8_t)(MATTER_EX_FLAG_I | MATTER_EX_FLAG_R | extra_ex_flags);
	ph.opcode = opcode;
	ph.exchange_id = exchange_id;
	ph.protocol_id = protocol_id;

	(void)matter_msg_header_encode(&mh, buf, cap, &mh_len);
	(void)matter_proto_header_encode(&ph, buf + mh_len, cap - mh_len, &ph_len);
	if (payload_len > 0u) {
		memcpy(buf + mh_len + ph_len, payload, payload_len);
	}
	return mh_len + ph_len + payload_len;
}

/** A plain PBKDFParamRequest-shaped message on the happy path. */
static size_t inbound_ok(uint8_t *buf, size_t cap, uint8_t opcode, uint32_t counter,
			 const uint8_t *payload, size_t payload_len)
{
	return inbound(buf, cap, opcode, counter, PEER_EXCHANGE_ID, MATTER_SESSION_ID_UNSECURED,
		       MATTER_PROTOCOL_SECURE_CHANNEL, 0u, 0u, payload, payload_len);
}

void test_matter_exchange(void)
{
	struct matter_exchange x;
	struct matter_exchange_in in;
	uint8_t msg[256];
	uint8_t out[256];
	uint8_t pt[256];
	size_t n;
	size_t out_len = 0u;
	static const uint8_t k_payload[] = {0x15, 0x30, 0x01, 0x20, 0xAA, 0xBB, 0x18};

	t_group("a message in, a reply out");
	{
		struct matter_msg_header mh;
		struct matter_proto_header ph;
		size_t mh_len = 0u;
		size_t ph_len = 0u;

		matter_exchange_init(&x, SEED, true);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 100u, k_payload, sizeof(k_payload));

		T_EQ("accepted", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_EQ("opcode", in.opcode, 0x20L);
		T_EQ("payload length", (long)in.payload_len, (long)sizeof(k_payload));
		T_OK("payload bytes", memcmp(in.payload, k_payload, sizeof(k_payload)) == 0);
		T_OK("peer asked to be acknowledged", in.ack_requested);
		T_OK("an acknowledgement is now owed", x.ack_pending);
		T_EQ("and it is for that counter", (long)x.ack_counter, 100L);
		T_EQ("bound to the peer's exchange", x.exchange_id, (long)PEER_EXCHANGE_ID);

		T_EQ("reply frames",
		     matter_exchange_reply(&x, 0x21u, k_payload, sizeof(k_payload), out,
					   sizeof(out), &out_len),
		     MATTER_OK);
		T_EQ("decode our own message header",
		     matter_msg_header_decode(out, out_len, &mh, &mh_len), MATTER_OK);
		T_EQ("unsecured session", mh.session_id, 0L);
		T_EQ("unicast", mh.security_flags & MATTER_SEC_SESSION_TYPE_MASK,
		     (long)MATTER_SESSION_TYPE_UNICAST);
		/* SessionManager.cpp:301-303: the responder addresses its reply to
		 * the initiator's ephemeral node id. Without this the peer cannot
		 * match the reply to its session and silently ignores it. */
		T_EQ("destination is a node id", mh.flags & MATTER_MSG_DSIZ_MASK,
		     (long)MATTER_MSG_DSIZ_NODE);
		T_OK("and it is the initiator's", mh.dest_node_id == PEER_NODE_ID);
		T_OK("we send no source node id of our own",
		     (mh.flags & MATTER_MSG_FLAG_S) == 0u);

		T_EQ("decode our own protocol header",
		     matter_proto_header_decode(out + mh_len, out_len - mh_len, &ph, &ph_len),
		     MATTER_OK);
		T_EQ("opcode", ph.opcode, 0x21L);
		T_EQ("same exchange", ph.exchange_id, (long)PEER_EXCHANGE_ID);
		/* The responder never claims to be the initiator, however many
		 * messages it goes on to send. */
		T_OK("I is clear", (ph.exchange_flags & MATTER_EX_FLAG_I) == 0u);
		T_OK("R is set", (ph.exchange_flags & MATTER_EX_FLAG_R) != 0u);
		T_OK("A is set", (ph.exchange_flags & MATTER_EX_FLAG_A) != 0u);
		T_EQ("acking the peer's counter", (long)ph.ack_counter, 100L);
		T_OK("nothing owed now", !x.ack_pending);
		T_EQ("payload carried through", (long)(out_len - mh_len - ph_len),
		     (long)sizeof(k_payload));
		T_OK("payload bytes", memcmp(out + mh_len + ph_len, k_payload,
					     sizeof(k_payload)) == 0);
		T_EQ("Secure Channel", ph.protocol_id, (long)MATTER_PROTOCOL_SECURE_CHANNEL);
	}

	t_group("a retransmission is acknowledged but not acted on twice");
	{
		matter_exchange_init(&x, SEED, true);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 7u, k_payload, sizeof(k_payload));
		T_EQ("first time", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_EQ("reply", matter_exchange_reply(&x, 0x21u, NULL, 0u, out, sizeof(out),
						    &out_len),
		     MATTER_OK);
		T_OK("ack consumed", !x.ack_pending);

		/* The peer did not hear the reply and sends the same message again. */
		T_EQ("second time is a duplicate", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_E_DUP);
		/* It still has to be acknowledged: the peer is retransmitting
		 * because it believes the ack was lost, and staying silent makes
		 * that true forever. */
		T_OK("but it is owed an acknowledgement again", x.ack_pending);
		T_EQ("for the same counter", (long)x.ack_counter, 7L);
	}

	t_group("what this layer refuses");
	{
		matter_exchange_init(&x, SEED, true);
		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID, 0x0005u,
			    MATTER_PROTOCOL_SECURE_CHANNEL, 0u, 0u, NULL, 0u);
		T_EQ("a secured session id", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_E_INVAL);

		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, 0x0001u, 0u, 0u, NULL, 0u);
		T_EQ("a protocol that is not Secure Channel",
		     matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_E_INVAL);

		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL,
			    MATTER_SESSION_TYPE_GROUP, 0u, NULL, 0u);
		T_EQ("a group session", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_E_INVAL);

		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL,
			    MATTER_SEC_FLAG_P, 0u, NULL, 0u);
		T_EQ("privacy we do not implement", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_E_INVAL);

		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL,
			    MATTER_SEC_FLAG_MX, 0u, NULL, 0u);
		T_EQ("message extensions we do not implement",
		     matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_E_INVAL);

		/* A vendor-scoped protocol id lives in a different namespace, so
		 * the protocol_id comparison would have meant nothing. */
		n = inbound(msg, sizeof(msg), 0x20u, 1u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL, 0u,
			    MATTER_EX_FLAG_V, NULL, 0u);
		T_EQ("a vendor-scoped protocol", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_E_INVAL);

		T_OK("none of them opened an exchange", !x.open);
	}

	t_group("one exchange at a time");
	{
		matter_exchange_init(&x, SEED, true);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 1u, NULL, 0u);
		T_EQ("the first one binds", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);

		n = inbound(msg, sizeof(msg), 0x20u, 2u, PEER_EXCHANGE_ID + 1u,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL, 0u, 0u,
			    NULL, 0u);
		T_EQ("a second commissioner is refused", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_E_STATE);
		T_EQ("still the first exchange", x.exchange_id, (long)PEER_EXCHANGE_ID);
	}

	t_group("standalone acknowledgement");
	{
		struct matter_proto_header ph;
		struct matter_msg_header mh;
		size_t mh_len = 0u;
		size_t ph_len = 0u;

		matter_exchange_init(&x, SEED, true);
		T_EQ("nothing to acknowledge yet",
		     matter_exchange_standalone_ack(&x, out, sizeof(out), &out_len),
		     MATTER_E_STATE);

		n = inbound_ok(msg, sizeof(msg), 0x24u, 42u, NULL, 0u);
		T_EQ("message", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_EQ("ack frames", matter_exchange_standalone_ack(&x, out, sizeof(out), &out_len),
		     MATTER_OK);

		(void)matter_msg_header_decode(out, out_len, &mh, &mh_len);
		T_EQ("decode", matter_proto_header_decode(out + mh_len, out_len - mh_len, &ph,
							  &ph_len),
		     MATTER_OK);
		T_EQ("StandaloneAck opcode", ph.opcode, (long)MATTER_SC_OP_ACK);
		T_EQ("empty payload", (long)(out_len - mh_len - ph_len), 0L);
		T_OK("carries the ack", (ph.exchange_flags & MATTER_EX_FLAG_A) != 0u);
		T_EQ("of the right counter", (long)ph.ack_counter, 42L);
		/* An ack that asked to be acknowledged would never terminate. */
		T_OK("does not request one back", (ph.exchange_flags & MATTER_EX_FLAG_R) == 0u);

		T_EQ("and only once",
		     matter_exchange_standalone_ack(&x, out, sizeof(out), &out_len),
		     MATTER_E_STATE);
	}

	t_group("an ack that could not be encoded is still owed");
	{
		size_t small_len = 0u;

		matter_exchange_init(&x, SEED, true);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 5u, k_payload, sizeof(k_payload));
		T_EQ("message", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_OK("owed", x.ack_pending);

		T_EQ("reply does not fit",
		     matter_exchange_reply(&x, 0x21u, k_payload, sizeof(k_payload), out, 8u,
					   &small_len),
		     MATTER_E_NOSPACE);
		/* Clearing the flag on a failed encode would drop the ack
		 * silently, and the peer would retransmit until it gave up. */
		T_OK("still owed", x.ack_pending);

		T_EQ("and a real buffer works",
		     matter_exchange_reply(&x, 0x21u, k_payload, sizeof(k_payload), out,
					   sizeof(out), &out_len),
		     MATTER_OK);
		T_OK("now consumed", !x.ack_pending);
	}

	t_group("over BLE, MRP is off entirely");
	{
		/*
		 * BTP is already reliable, so Matter does not run MRP on top of it:
		 * AllowsMRP() is "the peer address is UDP" (SecureSession.h:161,
		 * UnauthenticatedSessionTable.h:87) and ExchangeContext.cpp:109-112
		 * only sets R when the session allows it.
		 *
		 * A real iPhone proved this the hard way: it sent
		 * PBKDFParamRequest with exchange flags 0x01 -- I only -- and
		 * dropped the link on a reply that came back with R set.
		 */
		struct matter_proto_header ph;
		struct matter_msg_header mh;
		size_t mh_len = 0u;
		size_t ph_len = 0u;

		matter_exchange_init(&x, SEED, false);
		/* The peer does not set R either; this mirrors the capture. */
		n = inbound(msg, sizeof(msg), 0x20u, 11u, PEER_EXCHANGE_ID,
			    MATTER_SESSION_ID_UNSECURED, MATTER_PROTOCOL_SECURE_CHANNEL, 0u, 0u,
			    k_payload, sizeof(k_payload));
		/* inbound() always sets R; clear it so the message matches what an
		 * iPhone actually sends. The exchange flags are the first byte of
		 * the protocol header, so find where that starts rather than
		 * assuming -- it moves when the message header carries a node id. */
		{
			struct matter_msg_header probe;
			size_t probe_len = 0u;

			T_EQ("locate the protocol header",
			     matter_msg_header_decode(msg, n, &probe, &probe_len), MATTER_OK);
			msg[probe_len] &= (uint8_t)~MATTER_EX_FLAG_R;
		}

		T_EQ("accepted", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_OK("peer did not request an ack", !in.ack_requested);
		T_OK("so none is owed", !x.ack_pending);

		T_EQ("reply frames",
		     matter_exchange_reply(&x, 0x21u, k_payload, sizeof(k_payload), out,
					   sizeof(out), &out_len),
		     MATTER_OK);
		(void)matter_msg_header_decode(out, out_len, &mh, &mh_len);
		T_EQ("decode", matter_proto_header_decode(out + mh_len, out_len - mh_len, &ph,
							  &ph_len),
		     MATTER_OK);
		T_OK("R is NOT set", (ph.exchange_flags & MATTER_EX_FLAG_R) == 0u);
		T_OK("A is NOT set", (ph.exchange_flags & MATTER_EX_FLAG_A) == 0u);
		T_OK("I still clear", (ph.exchange_flags & MATTER_EX_FLAG_I) == 0u);
		T_EQ("still the same exchange", ph.exchange_id, (long)PEER_EXCHANGE_ID);

		/* Nothing to acknowledge means no standalone ack exists either. */
		T_EQ("no standalone ack over BLE",
		     matter_exchange_standalone_ack(&x, out, sizeof(out), &out_len),
		     MATTER_E_STATE);

		/* Even if a peer DID set R, MRP stays off: the transport decides. */
		matter_exchange_init(&x, SEED, false);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 12u, NULL, 0u);
		T_EQ("accepted", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_OK);
		T_OK("peer asked", in.ack_requested);
		T_OK("but nothing is owed", !x.ack_pending);
		T_EQ("reply", matter_exchange_reply(&x, 0x21u, NULL, 0u, out, sizeof(out),
						    &out_len),
		     MATTER_OK);
		(void)matter_msg_header_decode(out, out_len, &mh, &mh_len);
		(void)matter_proto_header_decode(out + mh_len, out_len - mh_len, &ph, &ph_len);
		T_OK("still no A", (ph.exchange_flags & MATTER_EX_FLAG_A) == 0u);
		T_OK("still no R", (ph.exchange_flags & MATTER_EX_FLAG_R) == 0u);
	}

	t_group("the secure session PASE hands over");
	{
		/*
		 * Everything here is what a commissioner does the moment PASE
		 * finishes: it stops talking in the clear, addresses us by the
		 * session id we announced, and opens a NEW exchange.
		 *
		 * The plaintext is built with our own encoders and sealed with
		 * matter_crypto_seal(), which is pinned against OpenSSL vectors in
		 * test_matter_crypto.c -- so this checks the wiring (which key,
		 * which node id, which session id), not the cipher.
		 */
		struct matter_session_keys keys;
		struct matter_msg_header mh;
		struct matter_proto_header ph;
		uint8_t plain[64];
		size_t plain_len = 0u;
		size_t sealed = 0u;

		for (size_t i = 0; i < MATTER_KEY_LEN; i++) {
			keys.i2r[i] = (uint8_t)(0x10u + i);
			keys.r2i[i] = (uint8_t)(0x40u + i);
			keys.attestation_challenge[i] = (uint8_t)(0x70u + i);
		}

		matter_exchange_init(&x, SEED, false);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 3u, NULL, 0u);
		T_EQ("PASE message first", matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)),
		     MATTER_OK);

		T_EQ("promote", matter_exchange_promote(&x, 0xABCDu, 0x1234u, &keys, 0x5EEDu),
		     MATTER_OK);
		T_OK("secure now", x.secure);
		/* PASE's exchange is finished; the peer opens a new one. */
		T_OK("exchange released", !x.open);

		/* Build an Interaction Model message the way the peer would. */
		memset(&ph, 0, sizeof(ph));
		ph.exchange_flags = MATTER_EX_FLAG_I;
		ph.opcode = 0x02u; /* ReadRequest */
		ph.exchange_id = 0x7777u;
		ph.protocol_id = MATTER_PROTOCOL_INTERACTION_MODEL;
		T_EQ("proto header", matter_proto_header_encode(&ph, plain, sizeof(plain),
								&plain_len),
		     MATTER_OK);
		plain[plain_len++] = 0x15u; /* a scrap of TLV payload */
		plain[plain_len++] = 0x18u;

		memset(&mh, 0, sizeof(mh));
		mh.flags = MATTER_MSG_DSIZ_NONE;
		mh.session_id = 0xABCDu; /* addressed to the id we announced */
		mh.security_flags = MATTER_SESSION_TYPE_UNICAST;
		mh.message_counter = 900u;
		T_EQ("seal", matter_crypto_seal(&mh, keys.i2r, MATTER_PASE_NODE_ID, plain, plain_len,
						msg, sizeof(msg), &sealed),
		     MATTER_OK);

		T_EQ("accepted", matter_exchange_recv(&x, msg, sealed, &in, pt, sizeof(pt)),
		     MATTER_OK);
		T_EQ("Interaction Model", in.protocol_id,
		     (long)MATTER_PROTOCOL_INTERACTION_MODEL);
		T_EQ("opcode survived decryption", in.opcode, 0x02L);
		T_EQ("payload length", (long)in.payload_len, 2L);
		T_OK("payload bytes", in.payload[0] == 0x15u && in.payload[1] == 0x18u);
		T_EQ("bound to the new exchange", x.exchange_id, 0x7777L);

		/* Wrong key must fail the tag, not produce plausible rubbish. */
		T_EQ("promote with r2i as the decrypt key",
		     matter_exchange_promote(&x, 0xABCDu, 0x1234u, &keys, 0x5EEDu), MATTER_OK);
		memcpy(x.keys.i2r, keys.r2i, MATTER_KEY_LEN);
		T_EQ("tag refuses", matter_exchange_recv(&x, msg, sealed, &in, pt, sizeof(pt)),
		     MATTER_E_TYPE);

		/* Once keys exist the clear channel is closed. */
		T_EQ("re-promote", matter_exchange_promote(&x, 0xABCDu, 0x1234u, &keys, 0x5EEDu),
		     MATTER_OK);
		n = inbound_ok(msg, sizeof(msg), 0x20u, 4u, NULL, 0u);
		T_EQ("cleartext is refused after PASE",
		     matter_exchange_recv(&x, msg, n, &in, pt, sizeof(pt)), MATTER_E_INVAL);

		/* A secure session id we never announced is not ours. */
		T_EQ("re-promote", matter_exchange_promote(&x, 0xABCDu, 0x1234u, &keys, 0x5EEDu),
		     MATTER_OK);
		mh.session_id = 0x0001u;
		(void)matter_crypto_seal(&mh, keys.i2r, MATTER_PASE_NODE_ID, plain, plain_len, msg,
					 sizeof(msg), &sealed);
		T_EQ("wrong session id refused",
		     matter_exchange_recv(&x, msg, sealed, &in, pt, sizeof(pt)), MATTER_E_INVAL);
	}

	t_group("replying before anything arrived");
	{
		matter_exchange_init(&x, SEED, true);
		T_EQ("has no exchange to reply on",
		     matter_exchange_reply(&x, 0x21u, NULL, 0u, out, sizeof(out), &out_len),
		     MATTER_E_STATE);
	}
}
