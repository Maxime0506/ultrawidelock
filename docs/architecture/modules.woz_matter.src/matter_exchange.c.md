<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_exchange.c`

@file matter_exchange.c — the unsecured exchange. See matter_exchange.h.

**depends on** [`modules/woz_matter/include/matter_exchange.h`](../modules.woz_matter.include/matter_exchange.h.md)

## API

### `static int check_msg_header(const struct matter_exchange *x, const struct matter_msg_header *h)`
`modules/woz_matter/src/matter_exchange.c:25`

Everything about a message header that disqualifies it from this layer.
Split out because it is a list of refusals rather than a computation, and
because every item is a thing an unauthenticated peer chose.

**called by** `matter_exchange_recv`

### `static bool exchange_is_ours(const struct matter_exchange *x, uint16_t id)`
`modules/woz_matter/src/matter_exchange.c:52`

Did this node open @p id? See @ref matter_exchange::init_exchange.

**called by** `exchange_remember`, `matter_exchange_recv`

### `static void exchange_remember(struct matter_exchange *x, uint16_t id)`
`modules/woz_matter/src/matter_exchange.c:63`

Remember it, dropping the oldest. Duplicates are not re-recorded.

**called by** `frame`  ·  **calls** `exchange_is_ours`

### `static int frame(struct matter_exchange *x, uint16_t protocol_id, uint8_t opcode, bool reliable, bool as_initiator, uint16_t init_exchange_id, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_exchange.c:274`

Frame one outbound message on this exchange.
@param reliable sets R. Everything in commissioning is reliable except a
standalone ack, which would otherwise ask to be acknowledged and never
terminate.
@param as_initiator sets I and uses @p init_exchange_id instead of the
peer's. Only a server-initiated exchange -- a subscription report --
does this; see matter_exchange_send_initiator().

**called by** `matter_exchange_reply`, `matter_exchange_send`, `matter_exchange_send_initiator`, `matter_exchange_standalone_ack`  ·  **calls** `exchange_remember`

<details><summary>Undocumented (8)</summary>

- `matter_exchange_init` — tested: matter exchange
- `matter_exchange_recv` — tested: matter exchange
- `matter_exchange_promote` — tested: matter exchange
- `matter_exchange_reply` — tested: matter exchange
- `matter_exchange_send` — tested: matter exchange
- `matter_exchange_send_initiator` — tested: matter exchange
- `matter_exchange_standalone_ack` — tested: matter exchange
- `matter_exchange_set_op_node_ids` — tested: matter exchange

</details>
