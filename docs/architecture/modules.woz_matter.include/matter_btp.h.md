<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_btp.h`

@file matter_btp.h — BTP, the Matter commissioning transport over BLE GATT.
A Matter message is far larger than a BLE ATT payload, so BTP chops it into
fragments, numbers them, and acknowledges them. This file is the framing
only: no GATT, no Zephyr, no timers. The 0xFFF6 service that carries it is a
separate piece.
handshake req   0x65 0x6C  versions[4]  mtu:u16  window:u8      (9 bytes)
handshake resp  0x65 0x6C  version:u8   fragment:u16  window:u8 (6 bytes)
data fragment   flags:u8  [ack:u8 if A]  seq:u8  [len:u16 if S]  payload
Little-endian, like the rest of Matter.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/src/matter_btp.c`](../modules.woz_matter.src/matter_btp.c.md)

## API

### `struct matter_btp_handshake_req`
`modules/woz_matter/include/matter_btp.h:84`

Central's offer.
@param versions unpacked one per element, high to low preference. A zero ends
the list, so a zero in slot 0 means the peer offered nothing.
@param mtu the negotiated ATT MTU, or 0 when the central could not determine
it -- which is a real case, not a malformed message (BleLayer.h:132).

### `struct matter_btp_handshake_resp`
`modules/woz_matter/include/matter_btp.h:91`

Peripheral's answer: what was actually selected.

### `struct matter_btp_rx`
`modules/woz_matter/include/matter_btp.h:133`

Inbound reassembler.
The reassembly area is CALLER-OWNED and its size is the hard ceiling on an
inbound message: a Start fragment declaring more than @c cap is refused
before a byte is copied, so a peer cannot choose this node's memory use.

### `struct matter_btp_tx`
`modules/woz_matter/include/matter_btp.h:177`

Outbound fragmenter. Borrows the message; nothing is copied, so @p msg must
outlive the walk.
