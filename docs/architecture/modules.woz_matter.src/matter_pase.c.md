<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_pase.c`

@file matter_pase.c — PASE message codec over Matter TLV.

**depends on** [`modules/woz_matter/include/matter_pase.h`](../modules.woz_matter.include/matter_pase.h.md), [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `static int ctx_tag(const struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_pase.c:52`

Context tag number of the loaded element, or -1 if it is not a context tag.

**called by** `decode_one`, `matter_pase_pake2_decode`, `matter_pase_pbkdf_req_decode`, `matter_pase_pbkdf_resp_decode`, `read_pbkdf_params`, `read_session_params`

### `static int get_fixed(const struct matter_tlv_reader *r, uint8_t *dst, size_t want)`
`modules/woz_matter/src/matter_pase.c:68`

Copy a byte string that must be exactly @p want long.

**called by** `decode_one`, `matter_pase_pake2_decode`, `matter_pase_pbkdf_req_decode`, `matter_pase_pbkdf_resp_decode`

### `static int read_session_params(struct matter_tlv_reader *r, struct matter_session_params *out)`
`modules/woz_matter/src/matter_pase.c:85`

Read a session-parameters structure the reader is currently sitting on.

**called by** `matter_pase_pbkdf_req_decode`, `matter_pase_pbkdf_resp_decode`  ·  **calls** `ctx_tag`

### `static int open_message(struct matter_tlv_reader *r, const uint8_t *buf, size_t len)`
`modules/woz_matter/src/matter_pase.c:121`

Open the outer anonymous structure every PASE message is wrapped in.

**called by** `decode_one`, `matter_pase_pake2_decode`, `matter_pase_pbkdf_req_decode`, `matter_pase_pbkdf_resp_decode`

### `int matter_pase_pbkdf_req_decode(const uint8_t *buf, size_t len, struct matter_pase_pbkdf_req *out)`
`modules/woz_matter/src/matter_pase.c:144`

Decode a PBKDF request message; extracts initiator random, session ID, and optional PBKDF
parameters; returns MATTER_E_STATE if required fields are missing and MATTER_E_INVAL if passcode
ID is not 0.

**calls** `ctx_tag`, `get_fixed`, `open_message`, `read_session_params`

### `int matter_pase_pbkdf_req_encode(const struct matter_pase_pbkdf_req *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_pase.c:223`

Encode a PBKDF request message with initiator random, session ID, optional passcode and session
parameters.

### `static int read_pbkdf_params(struct matter_tlv_reader *r, struct matter_pase_pbkdf_resp *out)`
`modules/woz_matter/src/matter_pase.c:247`

Decode a PBKDF parameters structure from the reader's current position; extracts iteration count
and salt, validating that iteration count is within the minimum and maximum allowed range;
returns MATTER_E_STATE if either field is missing.

**called by** `matter_pase_pbkdf_resp_decode`  ·  **calls** `ctx_tag`

### `int matter_pase_pbkdf_resp_decode(const uint8_t *buf, size_t len, struct matter_pase_pbkdf_resp *out)`
`modules/woz_matter/src/matter_pase.c:305`

Decode a PBKDF response message: extract initiator and responder randomness, responder session
ID, PBKDF parameters, and session parameters. Require all three random and session ID fields.

**calls** `ctx_tag`, `get_fixed`, `open_message`, `read_pbkdf_params`, `read_session_params`

### `int matter_pase_pbkdf_resp_encode(const struct matter_pase_pbkdf_resp *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_pase.c:380`

Encode a PBKDF response message with responder random, session ID, and optional PBKDF parameters
(salt and iteration count); validates that salt and iteration counts are within allowed ranges if
present.

### `static int decode_one(const uint8_t *buf, size_t len, unsigned int tag, uint8_t *dst, size_t want)`
`modules/woz_matter/src/matter_pase.c:418`

Decode a single context-tagged field from a PASE message; scans the entire message for the given
tag and copies its fixed-length value to dst, returning MATTER_E_STATE if the tag is not found.

**called by** `matter_pase_pake1_decode`, `matter_pase_pake3_decode`  ·  **calls** `ctx_tag`, `get_fixed`, `open_message`

### `int matter_pase_pake1_decode(const uint8_t *buf, size_t len, struct matter_pase_pake1 *out)`
`modules/woz_matter/src/matter_pase.c:445`

Decode the initiator's P_A point from a PASE Pake1 message.

**calls** `decode_one`

### `int matter_pase_pake1_encode(const struct matter_pase_pake1 *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_pase.c:457`

Encode a PAKE1 message containing the initiator's ephemeral public point.

### `int matter_pase_pake2_decode(const uint8_t *buf, size_t len, struct matter_pase_pake2 *out)`
`modules/woz_matter/src/matter_pase.c:476`

Decode a PAKE2 message containing the responder's ephemeral public point and confirmation hash;
returns MATTER_E_STATE if either field is missing.

**calls** `ctx_tag`, `get_fixed`, `open_message`

### `int matter_pase_pake2_encode(const struct matter_pase_pake2 *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_pase.c:521`

Encode a PAKE2 message containing the responder's ephemeral public point and confirmation hash.

### `int matter_pase_pake3_decode(const uint8_t *buf, size_t len, struct matter_pase_pake3 *out)`
`modules/woz_matter/src/matter_pase.c:540`

Decode the committer's C_A hash from a PASE Pake3 message.

**calls** `decode_one`

### `int matter_pase_pake3_encode(const struct matter_pase_pake3 *r, uint8_t *buf, size_t cap, size_t *written)`
`modules/woz_matter/src/matter_pase.c:552`

Encode a PAKE3 message containing the initiator's confirmation hash.
