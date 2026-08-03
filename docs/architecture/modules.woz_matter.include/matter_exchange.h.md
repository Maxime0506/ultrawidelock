<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_exchange.h`

@file matter_exchange.h — the unsecured exchange PASE runs on.
Between BTP (a byte pipe) and PASE (five messages) sits the part that makes a
Matter message a message: which session it belongs to, which exchange, whether
it is a duplicate, and whether the peer is owed an acknowledgement.
in    message header | protocol header | payload
out   message header | protocol header | payload
This handles exactly one exchange on the UNSECURED session, which is all
commissioning needs before PASE finishes: session id 0, no encryption, the
peer as initiator and this node as responder. Secure sessions are a different
object -- they carry keys and a different counter -- and arrive with CASE.
It deliberately does not know what PASE is. It reports the opcode and hands
back the payload; the caller decides what to answer. That keeps the framing
testable on its own, and means CASE will reuse it rather than fork it.
No timers here either. Duplicate suppression and the ack bookkeeping are
state, not scheduling; retransmission is matter_mrp.h's, driven by whoever
owns a clock.

**depends on** [`modules/woz_matter/include/matter_crypto.h`](matter_crypto.h.md), [`modules/woz_matter/include/matter_mrp.h`](matter_mrp.h.md), [`modules/woz_matter/include/matter_msg.h`](matter_msg.h.md), [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/src/matter_exchange.c`](../modules.woz_matter.src/matter_exchange.c.md)

## API

### `struct matter_exchange`
`modules/woz_matter/include/matter_exchange.h:82`

Matter exchange state including secure session keys, local and peer session IDs, MRP settings,
exchange ID tracking, message counters, and acknowledgement tracking for a single commissioner
session.

### `struct matter_exchange_in`
`modules/woz_matter/include/matter_exchange.h:211`

What a received message turned out to be.
@ref opcode, @ref protocol_id, @ref exchange_id and @ref initiator are set
even when matter_exchange_recv() goes on to REFUSE the message, so a caller
can log what it turned away. The rest is meaningful only on MATTER_OK.
