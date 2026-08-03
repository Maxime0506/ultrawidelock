<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_exchange.c`

@file matter_exchange.c — the unsecured exchange. See matter_exchange.h.

**depends on** [`modules/woz_matter/include/matter_exchange.h`](../modules.woz_matter.include/matter_exchange.h.md)

## API

### `void matter_exchange_init(struct matter_exchange *x, uint32_t entropy, bool mrp)`
`modules/woz_matter/src/matter_exchange.c:15`

Initialize a Matter exchange: clear state, set MRP mode, init the message counter with the given
entropy, and init the MRP window.

### `static int check_msg_header(const struct matter_exchange *x, const struct matter_msg_header *h)`
`modules/woz_matter/src/matter_exchange.c:29`

Everything about a message header that disqualifies it from this layer.
Split out because it is a list of refusals rather than a computation, and
because every item is a thing an unauthenticated peer chose.

**called by** `matter_exchange_recv`

### `static bool exchange_is_ours(const struct matter_exchange *x, uint16_t id)`
`modules/woz_matter/src/matter_exchange.c:56`

Did this node open @p id? See @ref matter_exchange::init_exchange.

**called by** `exchange_remember`, `matter_exchange_recv`

### `static void exchange_remember(struct matter_exchange *x, uint16_t id)`
`modules/woz_matter/src/matter_exchange.c:67`

Remember it, dropping the oldest. Duplicates are not re-recorded.

**called by** `frame`  ·  **calls** `exchange_is_ours`

### `int matter_exchange_recv(struct matter_exchange *x, const uint8_t *msg, size_t len, struct matter_exchange_in *in, uint8_t *pt, size_t pt_cap)`
`modules/woz_matter/src/matter_exchange.c:90`

Receive and decode a message on this exchange: decode and validate the message header, decrypt if
secure, decode the protocol header, validate exchange ID and state, check replay window, and
return the parsed message. On secure sessions, the protocol header is decrypted; the plaintext
buffer must be provided and must be large enough. Return MATTER_OK on success; MATTER_E_INVAL if
pointers are null or structure is invalid; MATTER_E_STATE if the exchange ID does not match and
the message is not a valid acknowledgement.

**calls** `check_msg_header`, `exchange_is_ours`

### `int matter_exchange_promote(struct matter_exchange *x, uint16_t local_id, uint16_t peer_id, const struct matter_session_keys *keys, uint32_t entropy)`
`modules/woz_matter/src/matter_exchange.c:251`

Promote an unsecured exchange to a secure session exchange: set secure flag, IDs, and keys;
reinit counter and MRP window with new entropy; initialize operational node IDs to PASE defaults;
clear open and ack_pending flags. Return MATTER_E_INVAL if pointers are null or local_id is
unsecured, MATTER_OK on success.

### `static int frame(struct matter_exchange *x, uint16_t protocol_id, uint8_t opcode, bool reliable, bool as_initiator, uint16_t init_exchange_id, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:292`

Frame one outbound message on this exchange.
@param reliable sets R. Everything in commissioning is reliable except a
standalone ack, which would otherwise ask to be acknowledged and never
terminate.
@param as_initiator sets I and uses @p init_exchange_id instead of the
peer's. Only a server-initiated exchange -- a subscription report --
does this; see matter_exchange_send_initiator().

**called by** `matter_exchange_reply`, `matter_exchange_send`, `matter_exchange_send_initiator`, `matter_exchange_standalone_ack`  ·  **calls** `exchange_remember`

### `int matter_exchange_reply(struct matter_exchange *x, uint8_t opcode, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:442`

Frame and send a reply on this exchange in the Secure Channel protocol, setting the initiator
flag to false and clearing the R and A exchange flags. Calls frame() with the given opcode and
payload.

**calls** `frame`

### `int matter_exchange_send(struct matter_exchange *x, uint16_t protocol_id, uint8_t opcode, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:452`

Frame one outbound message on this exchange with the given protocol ID and opcode.

**calls** `frame`

### `int matter_exchange_send_initiator(struct matter_exchange *x, uint16_t exchange_id, uint16_t protocol_id, uint8_t opcode, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:464`

Frame one outbound message on this exchange as the initiator, setting the exchange ID in the
message header.

**calls** `frame`

### `int matter_exchange_standalone_ack(struct matter_exchange *x, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:476`

Send a standalone MRP acknowledgment on this exchange if one is pending and MRP is enabled;
returns MATTER_E_STATE if no ack is pending or MRP is not active.

**calls** `frame`

### `void matter_exchange_set_op_node_ids(struct matter_exchange *x, uint64_t local, uint64_t peer)`
`modules/woz_matter/src/matter_exchange.c:493`

Set the operational node IDs for this exchange; used to populate node ID fields in secure channel
messages.
