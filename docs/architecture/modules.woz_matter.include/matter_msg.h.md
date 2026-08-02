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

<details><summary>Undocumented (3)</summary>

- `matter_msg_header`
- `matter_proto_header`
- `matter_counter`

</details>
