<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_btp.c`

@file matter_btp.c — BTP handshake codec, fragmenter and reassembler.

**depends on** [`modules/woz_matter/include/matter_btp.h`](../modules.woz_matter.include/matter_btp.h.md)

## API

### `static uint16_t rd16(const uint8_t *p)`
`modules/woz_matter/src/matter_btp.c:28`

Read a little-endian 16-bit unsigned integer from the buffer.

**called by** `matter_btp_req_decode`, `matter_btp_resp_decode`, `matter_btp_rx_fragment`

### `static void wr16(uint8_t *p, uint16_t v)`
`modules/woz_matter/src/matter_btp.c:36`

Write a little-endian 16-bit unsigned integer to the buffer.

**called by** `matter_btp_req_encode`, `matter_btp_resp_encode`, `matter_btp_tx_next`

### `int matter_btp_req_decode(const uint8_t *buf, size_t len, struct matter_btp_handshake_req *out)`
`modules/woz_matter/src/matter_btp.c:47`

Decode a Matter BTP handshake request from a buffer: extract protocol versions (8 4-bit slots
across 4 bytes), MTU, and window size. Returns MATTER_OK on success, MATTER_E_INVAL if header
check fails, MATTER_E_TRUNC if buffer is too short.

**calls** `rd16`

### `int matter_btp_req_encode(const struct matter_btp_handshake_req *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_btp.c:78`

Encode a Matter BTP handshake request into a buffer: write header check bytes, 8 protocol
versions (4-bit slots, even indices in low nibble), MTU, and window size. Returns MATTER_OK on
success, MATTER_E_NOSPACE if buffer capacity is insufficient.

**calls** `wr16`

### `int matter_btp_resp_decode(const uint8_t *buf, size_t len, struct matter_btp_handshake_resp *out)`
`modules/woz_matter/src/matter_btp.c:114`

Decode a Matter BTP handshake response from a buffer: extract selected version, fragment size,
and window size. Returns MATTER_OK on success, MATTER_E_INVAL if header check fails,
MATTER_E_TRUNC if buffer is too short.

**calls** `rd16`

### `int matter_btp_resp_encode(const struct matter_btp_handshake_resp *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_btp.c:137`

Encode a Matter BTP handshake response into a buffer: write header check bytes, selected version,
fragment size, and window size. Returns MATTER_OK on success, MATTER_E_NOSPACE if buffer capacity
is insufficient.

**calls** `wr16`

### `int matter_btp_accept(const struct matter_btp_handshake_req *req, uint16_t local_att_mtu, uint8_t local_window, struct matter_btp_handshake_resp *out)`
`modules/woz_matter/src/matter_btp.c:165`

Accept a BLE transport handshake request and produce a response. Negotiate the highest mutually
supported BTP version (V4), derive the fragment size from both MTU values (clamped to min/max
bounds), and use the minimum window size from both sides. Return MATTER_E_INVAL if pointers are
null, MATTER_E_TYPE if no common version exists, MATTER_OK on success.

### `void matter_btp_rx_init(struct matter_btp_rx *rx, uint8_t *buf, size_t cap, uint8_t first_seq)`
`modules/woz_matter/src/matter_btp.c:220`

Initialize a BTP RX reassembler: clear state, set buffer and capacity, mark idle, and set the
expected first sequence number.

### `void matter_btp_rx_reset(struct matter_btp_rx *rx)`
`modules/woz_matter/src/matter_btp.c:236`

Reset a BTP RX reassembler for the next message: clear buffered data and state flags, but
preserve sequence numbers (they belong to the connection, not the message).

### `int matter_btp_rx_fragment(struct matter_btp_rx *rx, const uint8_t *frag, size_t len)`
`modules/woz_matter/src/matter_btp.c:257`

Process an incoming BTP fragment: validate sequence number, extract flags and optional ACK,
accumulate payload across fragments, and detect message completion. Enforces strict ordering (no
reordering or gaps), prevents receiver restart mid-message, and caps total payload against
declared length. Returns MATTER_OK on continuation, MATTER_END on completion, MATTER_E_STATE on
sequence error or mid-message restart, MATTER_E_TRUNC on truncation, MATTER_E_NOSPACE on buffer
overflow.

**calls** `rd16`

### `int matter_btp_tx_init(struct matter_btp_tx *tx, const uint8_t *msg, size_t len, uint16_t fragment_size, uint8_t first_seq)`
`modules/woz_matter/src/matter_btp.c:381`

Initialize a BTP TX fragmenter with a message of length <= 0xFFFF, a fragment size within min/max
bounds. Return MATTER_E_INVAL if pointers/sizes are invalid, MATTER_OK on success.

### `int matter_btp_tx_next(struct matter_btp_tx *tx, const uint8_t *ack, uint8_t *out, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_btp.c:415`

Encode the next BTP transmission fragment from buffered message: emit flags, optional ACK,
sequence number, and payload chunk. Splits message across fragments respecting fragment_size;
Start fragment includes declared length, End fragment marks completion. Returns MATTER_OK on
success, MATTER_END when all bytes sent, MATTER_E_NOSPACE if fragment or output buffer capacity
exceeded.

**calls** `wr16`

### `int matter_btp_standalone_ack(uint8_t ack, uint8_t seq, uint8_t *out, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_btp.c:493`

Encode a standalone BTP acknowledgement: set the ACK flag, write the ack and seq fields. Return
MATTER_E_INVAL if out is null, MATTER_E_NOSPACE if cap < 3, MATTER_OK on success; write the
3-byte frame size if written is not null.
