<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_case.c`

*No module docstring. First commit: "woz_matter: CASE, the responder's first half".*

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_case.h`](../modules.woz_matter.include/matter_case.h.md), [`modules/woz_matter/include/matter_crypto.h`](../modules.woz_matter.include/matter_crypto.h.md), [`modules/woz_matter/include/matter_fabric.h`](../modules.woz_matter.include/matter_fabric.h.md), [`modules/woz_matter/include/matter_im.h`](../modules.woz_matter.include/matter_im.h.md), [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `static int take_bytes(const struct matter_tlv_reader *r, const uint8_t **out, size_t want)`
`modules/woz_matter/src/matter_case.c:74`

Borrow one octet string of an expected length out of the loaded element.

**called by** `matter_case_sigma1_decode`

<details><summary>Undocumented (5)</summary>

- `matter_case_operational_ipk` — tested: matter case
- `matter_case_destination_id` — tested: matter case
- `matter_case_sigma1_decode` — tested: matter case
- `matter_case_sigma2_encode` — tested: matter case
- `matter_case_sigma3_open`

</details>
