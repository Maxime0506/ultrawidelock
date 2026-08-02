<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_tlv.c`

@file matter_tlv.c — Matter TLV codec, encoder then decoder.
Control byte = tag control (top 3 bits) | element type (bottom 5). Then the
tag octets, then the value. Everything multi-octet is little-endian.

**depends on** [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `static bool fail(struct matter_tlv_writer *w, int rc)`
`modules/woz_matter/src/matter_tlv.c:69`

Latch the first error and report whether the writer is still usable.

**called by** `matter_tlv_end_container`, `matter_tlv_start_container`, `matter_tlv_writer_finish`, `put_string`, `put_tag`, `room`

### `static void put_le(struct matter_tlv_writer *w, uint64_t v, size_t n)`
`modules/woz_matter/src/matter_tlv.c:95`

Little-endian, n in 1/2/4/8.

**called by** `matter_tlv_put_i64`, `matter_tlv_put_u64`, `put_string`, `put_tag`

### `static bool put_tag(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t element_type)`
`modules/woz_matter/src/matter_tlv.c:114`

Emit the control byte and tag octets.
The tag control is chosen here, and the choice is where an encoder usually
goes wrong: a profile tag is written implicit only when it matches the
writer's nominated implicit profile, common when the profile is 0, and fully
qualified otherwise. A fully-qualified tag splits the 32-bit profile ID into
vendor (high 16) then profile number (low 16), each little-endian, ahead of
the tag number -- verified against CHIP's Encoding3, where profile
0xAABBCCDD and tag 1 encode as BB AA DD CC 01 00.

**called by** `matter_tlv_put_bool`, `matter_tlv_put_i64`, `matter_tlv_put_null`, `matter_tlv_put_u64`, `matter_tlv_start_container`, `put_string`  ·  **calls** `fail`, `put_le`, `room`, `tag_number`, `tag_profile`

### `static bool live(struct matter_tlv_writer *w)`
`modules/woz_matter/src/matter_tlv.c:160`

True while the writer is healthy. Every public entry point starts here.

**called by** `matter_tlv_end_container`, `matter_tlv_put_bool`, `matter_tlv_put_i64`, `matter_tlv_put_null`, `matter_tlv_put_u64`, `matter_tlv_start_container`, `put_string`

### `static int put_string(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type_len1, const void *data, size_t len)`
`modules/woz_matter/src/matter_tlv.c:263`

Shared body for the two string types; they differ only in the element-type base.

**called by** `matter_tlv_put_bytes`, `matter_tlv_put_utf8`  ·  **calls** `fail`, `live`, `put_le`, `put_raw`, `put_tag`, `room`

### `struct elem`
`modules/woz_matter/src/matter_tlv.c:375`

One parsed element. Purely local; the reader copies the fields it keeps.

### `static int parse_at(const struct matter_tlv_reader *r, size_t off, struct elem *e)`
`modules/woz_matter/src/matter_tlv.c:410`

Parse the element whose control byte is at @p off.
Does not follow container bodies: for a container, end == body_off and the
caller decides whether to enter or skip. That is what keeps this function
non-recursive.

**called by** `matter_tlv_next`, `scan_past_level_end`  ·  **calls** `fits`, `read_le`

### `static int scan_past_level_end(const struct matter_tlv_reader *r, size_t from, size_t *out)`
`modules/woz_matter/src/matter_tlv.c:512`

Walk forward from @p from to just past the end-of-container marker that
closes the level @p from sits in.
The nesting counter is the whole trick, and it is capped: deeply nested input
costs one loop iteration per element and never a stack frame.

**called by** `matter_tlv_exit`, `matter_tlv_next`  ·  **calls** `fits`, `parse_at`

<details><summary>Undocumented (32)</summary>

- `tag_profile`
- `tag_number`
- `room`
- `put_raw`
- `matter_tlv_writer_init` — tested: matter addnoc; matter im; matter im invoke; matter tlv
- `matter_tlv_writer_set_implicit_profile` — tested: matter tlv
- `matter_tlv_put_bool` — tested: matter im invoke; matter tlv
- `matter_tlv_put_null` — tested: matter tlv
- `matter_tlv_put_i64` — tested: matter tlv
- `matter_tlv_put_u64` — tested: matter im; matter im invoke; matter tlv
- `matter_tlv_put_utf8` — tested: matter im invoke; matter tlv
- `matter_tlv_put_bytes` — tested: matter tlv
- `matter_tlv_start_container` — tested: matter im; matter im invoke; matter tlv
- `matter_tlv_end_container` — tested: matter im; matter im invoke; matter tlv
- `matter_tlv_writer_finish` — tested: matter addnoc; matter im; matter im invoke; matter tlv
- `fits`
- `read_le`
- `matter_tlv_reader_init` — tested: matter attest; matter im; matter tlv
- `matter_tlv_reader_set_implicit_profile` — tested: matter tlv
- `matter_tlv_next` — tested: matter attest; matter im; matter tlv
- `matter_tlv_tag` — tested: matter attest; matter im; matter tlv
- `matter_tlv_element_type` — tested: matter attest; matter tlv
- `matter_tlv_is_container` — tested: matter tlv
- `matter_tlv_get_bool` — tested: matter tlv
- `matter_tlv_get_u64` — tested: matter attest; matter im; matter tlv
- `matter_tlv_get_i64` — tested: matter tlv
- `get_span`
- `matter_tlv_get_bytes` — tested: matter attest; matter tlv
- `matter_tlv_get_utf8` — tested: matter tlv
- `matter_tlv_enter` — tested: matter attest; matter im; matter tlv
- `matter_tlv_exit` — tested: matter tlv
- `matter_tlv_put_encoded`

</details>
