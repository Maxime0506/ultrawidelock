<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_msg.c`

@file matter_msg.c — Matter message and protocol header codec.

**depends on** [`modules/woz_matter/include/matter_msg.h`](../modules.woz_matter.include/matter_msg.h.md)

## API

### `static uint16_t rd16(const uint8_t *p)`
`modules/woz_matter/src/matter_msg.c:20`

Read a little-endian 16-bit unsigned integer from buffer p.

**called by** `matter_msg_header_decode`, `matter_proto_header_decode`

### `static uint32_t rd32(const uint8_t *p)`
`modules/woz_matter/src/matter_msg.c:28`

Read a little-endian 32-bit unsigned integer from buffer p.

**called by** `matter_msg_header_decode`, `matter_proto_header_decode`, `rd64`

### `static uint64_t rd64(const uint8_t *p)`
`modules/woz_matter/src/matter_msg.c:37`

Read a little-endian 64-bit unsigned integer by reading two 32-bit halves and combining them.

**called by** `matter_msg_header_decode`  ·  **calls** `rd32`

### `static void wr16(uint8_t *p, uint16_t v)`
`modules/woz_matter/src/matter_msg.c:45`

Write a little-endian 16-bit unsigned integer v to buffer p.

**called by** `matter_msg_header_encode`, `matter_proto_header_encode`

### `static void wr32(uint8_t *p, uint32_t v)`
`modules/woz_matter/src/matter_msg.c:54`

Write a little-endian 32-bit unsigned integer v to buffer p.

**called by** `matter_msg_header_encode`, `matter_proto_header_encode`, `wr64`

### `static void wr64(uint8_t *p, uint64_t v)`
`modules/woz_matter/src/matter_msg.c:65`

Write a little-endian 64-bit unsigned integer by writing two 32-bit halves.

**called by** `matter_msg_header_encode`  ·  **calls** `wr32`

### `static size_t msg_header_len(uint8_t flags)`
`modules/woz_matter/src/matter_msg.c:79`

Header length implied by a message flags byte, or 0 if the byte is invalid.
Rejecting DSIZ 3 here rather than treating it as "no destination" matters: it
is reserved, so a peer using it is either broken or probing, and silently
accepting would put this decoder's idea of the payload offset out of step
with the sender's.

**called by** `matter_msg_header_decode`, `matter_msg_header_encode`

### `static size_t proto_header_len(uint8_t exchange_flags)`
`modules/woz_matter/src/matter_msg.c:108`

Calculate the encoded length of a protocol header given the exchange flags; adds 2 bytes if the V
flag (vendor ID) is set and 4 bytes if the A flag (ack) is set.

**called by** `matter_proto_header_decode`, `matter_proto_header_encode`

### `bool matter_msg_is_secure(const struct matter_msg_header *h)`
`modules/woz_matter/src/matter_msg.c:125`

Return true if this message is encrypted; session ID 0 is unsecured unicast, all group sessions
are secured regardless of ID.

### `void matter_counter_init(struct matter_counter *c, uint32_t entropy, enum matter_counter_kind kind)`
`modules/woz_matter/src/matter_msg.c:241`

Initialize a counter to track message sequence numbers; the first value handed out will be one
higher than the entropy seed to ensure it is never zero.

### `int matter_counter_next(struct matter_counter *c, uint32_t *out)`
`modules/woz_matter/src/matter_msg.c:257`

Retrieve the next counter value and increment it; for session counters, returns MATTER_E_STATE if
the counter has wrapped to UINT32_MAX to prevent AEAD nonce reuse.

### `int matter_proto_header_decode(const uint8_t *buf, size_t len, struct matter_proto_header *h, size_t *consumed)`
`modules/woz_matter/src/matter_msg.c:279`

Decode a Matter protocol exchange header from a buffer: extract flags, opcode, exchange ID, and
conditional fields (vendor ID, protocol ID, ack counter). Calculate consumed bytes and validate
truncation. Return an error code.

**calls** `proto_header_len`, `rd16`, `rd32`

### `int matter_proto_header_encode(const struct matter_proto_header *h, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_msg.c:325`

Encode a Matter protocol exchange header into a buffer with the given flags, opcode, exchange ID,
and conditional fields (vendor ID, protocol ID, ack counter). Return an error code.

**calls** `proto_header_len`, `wr16`, `wr32`

<details><summary>Undocumented (2)</summary>

- `matter_msg_header_decode` — tested: matter exchange; matter msg
- `matter_msg_header_encode` — tested: matter msg

</details>
