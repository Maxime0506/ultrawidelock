<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_msg.h`

@file matter_msg.h — Matter message header and protocol (exchange) header.
Two headers, one wire format. The message header is the part that travels in
clear even on a secure session; the protocol header sits at the front of the
(decrypted) payload and names the exchange the message belongs to.
message header   flags:u8  session_id:u16  security_flags:u8  counter:u32
[source_node_id:u64 if S]  [dest:u64|u16 by DSIZ]
protocol header  exchange_flags:u8  opcode:u8  exchange_id:u16
[vendor_id:u16 if V]  protocol_id:u16  [ack_counter:u32 if A]
All little-endian.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_crypto.h`](matter_crypto.h.md), [`modules/woz_matter/include/matter_exchange.h`](matter_exchange.h.md), [`modules/woz_matter/src/matter_msg.c`](../modules.woz_matter.src/matter_msg.c.md)

## API

### `struct matter_msg_header`
`modules/woz_matter/include/matter_msg.h:87`

Matter message header decoded from the wire: flags, session ID, security flags, message counter,
optional source/destination node ID (unicast) or group ID (multicast).

### `struct matter_proto_header`
`modules/woz_matter/include/matter_msg.h:104`

Matter protocol/exchange header decoded from the message body: exchange flags, opcode, exchange
ID, optional vendor ID, protocol ID, and optional ACK counter.

### `struct matter_counter`
`modules/woz_matter/include/matter_msg.h:164`

RX/TX counter state: the last value used and the counter kind (unsecured or session).
