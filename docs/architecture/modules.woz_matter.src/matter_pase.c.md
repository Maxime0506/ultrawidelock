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

<details><summary>Undocumented (12)</summary>

- `matter_pase_pbkdf_req_decode` — tested: matter pase
- `matter_pase_pbkdf_req_encode` — tested: matter pase
- `read_pbkdf_params`
- `matter_pase_pbkdf_resp_decode` — tested: matter pase
- `matter_pase_pbkdf_resp_encode` — tested: matter pase
- `decode_one`
- `matter_pase_pake1_decode` — tested: matter pase
- `matter_pase_pake1_encode` — tested: matter pase
- `matter_pase_pake2_decode` — tested: matter pase
- `matter_pase_pake2_encode` — tested: matter pase
- `matter_pase_pake3_decode` — tested: matter pase
- `matter_pase_pake3_encode` — tested: matter pase

</details>
