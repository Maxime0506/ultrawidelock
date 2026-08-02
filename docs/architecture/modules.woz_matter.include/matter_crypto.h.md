<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_crypto.h`

@file matter_crypto.h — Matter message crypto: nonce, AES-CCM, session keys.
Matter secures every message with AES-128-CCM: a 13-byte nonce built from
fields the peer can see, a 16-byte tag, and the plaintext message header as
additional authenticated data so the routing fields cannot be edited in
flight.
nonce  security_flags:u8  message_counter:u32  node_id:u64   (little-endian)
aad    the message header exactly as it appears on the wire
keys   HKDF-SHA256(secret, salt, "SessionKeys") -> i2r | r2i | challenge

**depends on** [`modules/woz_matter/include/matter_msg.h`](matter_msg.h.md), [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_exchange.h`](matter_exchange.h.md), [`modules/woz_matter/include/matter_pase_sm.h`](matter_pase_sm.h.md), [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md), [`modules/woz_matter/src/matter_crypto.c`](../modules.woz_matter.src/matter_crypto.c.md)

## API

### `struct matter_session_keys`
`modules/woz_matter/include/matter_crypto.h:87`

Session keys, in the order the one HKDF output supplies them.
