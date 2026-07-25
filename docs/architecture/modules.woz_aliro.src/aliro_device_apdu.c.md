<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_device_apdu.c`

Implementation of the device-side Access-Protocol wire codec declared in
aliro_device_apdu.h: ISO7816 case-4 unwrapping, status-word appending, parsers
for the reader's AUTH0, AUTH1 and EXCHANGE command TLVs, and builders for the
three device responses. Every function is bounds-checked byte manipulation over
caller-owned buffers with no allocation, so it round-trips against the reader's
own builders and parsers in aliro_apdu.c under the host tests.

**depends on** [`modules/woz_aliro/include/aliro_device_apdu.h`](../modules.woz_aliro.include/aliro_device_apdu.h.md)

<details><summary>Undocumented (8)</summary>

- `aliro_apdu_unwrap` — tested: codec
- `aliro_apdu_append_sw` — tested: codec
- `aliro_dev_parse_auth0_cmd` — tested: codec
- `aliro_dev_parse_auth1_cmd` — tested: codec
- `aliro_dev_parse_exchange_cmd` — tested: codec
- `aliro_dev_build_auth0_resp` — tested: codec
- `aliro_dev_build_auth1_resp` — tested: codec
- `aliro_dev_build_exchange_resp`

</details>
