<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_tlv.c`

@file matter_tlv.c — Matter TLV codec, encoder then decoder.
Control byte = tag control (top 3 bits) | element type (bottom 5). Then the
tag octets, then the value. Everything multi-octet is little-endian.

**depends on** [`modules/woz_matter/include/matter_tlv.h`](../modules.woz_matter.include/matter_tlv.h.md)

## API

### `static uint32_t tag_profile(matter_tlv_tag_t tag)`
`modules/woz_matter/src/matter_tlv.c:61`

Extract the profile (upper 32 bits) from a qualified tag.

**called by** `matter_tlv_put_encoded`, `put_tag`

### `static uint32_t tag_number(matter_tlv_tag_t tag)`
`modules/woz_matter/src/matter_tlv.c:69`

Extract the number (lower 32 bits) from a qualified tag.

**called by** `matter_tlv_put_encoded`, `put_tag`

### `static bool fail(struct matter_tlv_writer *w, int rc)`
`modules/woz_matter/src/matter_tlv.c:75`

Latch the first error and report whether the writer is still usable.

**called by** `matter_tlv_end_container`, `matter_tlv_start_container`, `matter_tlv_writer_finish`, `put_string`, `put_tag`, `room`

### `static bool room(struct matter_tlv_writer *w, size_t n)`
`modules/woz_matter/src/matter_tlv.c:88`

Test whether writing n bytes would exceed the writer's capacity.
Fails the writer and latches the error code if buffer is NULL or space is exhausted; returns true
if write can proceed.

**called by** `matter_tlv_end_container`, `matter_tlv_put_i64`, `matter_tlv_put_u64`, `put_string`, `put_tag`  ·  **calls** `fail`

### `static void put_raw(struct matter_tlv_writer *w, const void *src, size_t n)`
`modules/woz_matter/src/matter_tlv.c:102`

Copy n bytes from src into the writer buffer at the current position, advancing the position.

**called by** `put_string`

### `static void put_le(struct matter_tlv_writer *w, uint64_t v, size_t n)`
`modules/woz_matter/src/matter_tlv.c:109`

Little-endian, n in 1/2/4/8.

**called by** `matter_tlv_put_i64`, `matter_tlv_put_u64`, `put_string`, `put_tag`

### `static bool put_tag(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t element_type)`
`modules/woz_matter/src/matter_tlv.c:128`

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
`modules/woz_matter/src/matter_tlv.c:174`

True while the writer is healthy. Every public entry point starts here.

**called by** `matter_tlv_end_container`, `matter_tlv_put_bool`, `matter_tlv_put_i64`, `matter_tlv_put_null`, `matter_tlv_put_u64`, `matter_tlv_start_container`, `put_string`

### `void matter_tlv_writer_init(struct matter_tlv_writer *w, uint8_t *buf, size_t cap)`
`modules/woz_matter/src/matter_tlv.c:184`

Initialize a TLV writer to build a buffer.
Sets writer to start-of-buffer state with no depth or errors; if buf is NULL, writes fail
silently.

### `void matter_tlv_writer_set_implicit_profile(struct matter_tlv_writer *w, uint32_t profile)`
`modules/woz_matter/src/matter_tlv.c:198`

Set the implicit tag profile for subsequent context-tag (CTX) writes.
Allows the writer to omit the profile qualifier in context tags, compressing the wire format.

### `int matter_tlv_put_bool(struct matter_tlv_writer *w, matter_tlv_tag_t tag, bool v)`
`modules/woz_matter/src/matter_tlv.c:213`

Append a boolean value to the TLV output.
Encodes as control byte indicating true or false, with the given tag.
Returns MATTER_TLV_E_INVAL if writer is NULL; returns the writer's cached error if previous write
failed.

**calls** `live`, `put_tag`

### `int matter_tlv_put_null(struct matter_tlv_writer *w, matter_tlv_tag_t tag)`
`modules/woz_matter/src/matter_tlv.c:228`

Append a null value to the TLV output.
Encodes as control byte with no value, with the given tag.
Returns MATTER_TLV_E_INVAL if writer is NULL; returns the writer's cached error if previous write
failed.

**calls** `live`, `put_tag`

### `int matter_tlv_put_i64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, int64_t v)`
`modules/woz_matter/src/matter_tlv.c:241`

Write a signed 64-bit integer to TLV with automatic width selection (1/2/4/8 bytes based on value
range). Returns first error encountered in writer, which is latched and persists across calls.

**calls** `live`, `put_le`, `put_tag`, `room`

### `int matter_tlv_put_u64(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint64_t v)`
`modules/woz_matter/src/matter_tlv.c:276`

Write an unsigned 64-bit integer to TLV with automatic width selection (1/2/4/8 bytes based on
value range). Returns first error encountered in writer, which is latched and persists across
calls.

**calls** `live`, `put_le`, `put_tag`, `room`

### `static int put_string(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type_len1, const void *data, size_t len)`
`modules/woz_matter/src/matter_tlv.c:307`

Shared body for the two string types; they differ only in the element-type base.

**called by** `matter_tlv_put_bytes`, `matter_tlv_put_utf8`  ·  **calls** `fail`, `live`, `put_le`, `put_raw`, `put_tag`, `room`

### `int matter_tlv_put_utf8(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const char *s, size_t len)`
`modules/woz_matter/src/matter_tlv.c:350`

Append a UTF-8 string to the TLV output.
Encodes length-prefixed text string with the given tag.
Returns the writer's cached error if previous write failed or no space.

**calls** `put_string`

### `int matter_tlv_put_bytes(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const uint8_t *b, size_t len)`
`modules/woz_matter/src/matter_tlv.c:361`

Append a byte string to the TLV output.
Encodes length-prefixed byte array with the given tag.
Returns the writer's cached error if previous write failed or no space.

**calls** `put_string`

### `int matter_tlv_start_container(struct matter_tlv_writer *w, matter_tlv_tag_t tag, uint8_t type)`
`modules/woz_matter/src/matter_tlv.c:374`

Begin a new TLV container (structure, array, or list) in the output.
Records the container tag and depth; the writer will track where to close it when end_container
is called.
Returns MATTER_TLV_E_INVAL if writer is NULL or type is invalid; returns MATTER_TLV_E_DEPTH if
nesting exceeds MATTER_TLV_MAX_DEPTH.

**calls** `fail`, `live`, `put_tag`

### `int matter_tlv_end_container(struct matter_tlv_writer *w)`
`modules/woz_matter/src/matter_tlv.c:397`

Close the current TLV container by writing the end-of-container control byte and decrementing
depth. Caller must have opened a container; fails if depth is already zero.

**calls** `fail`, `live`, `room`

### `int matter_tlv_writer_finish(struct matter_tlv_writer *w, size_t *out_len)`
`modules/woz_matter/src/matter_tlv.c:421`

Finalize TLV encoding and report the encoded byte count.
Validates all containers have been closed (depth is zero).
Returns MATTER_TLV_E_INVAL if writer is NULL; returns MATTER_TLV_E_STATE if containers remain
open; returns the writer's cached error if previous write failed.

**calls** `fail`

### `struct elem`
`modules/woz_matter/src/matter_tlv.c:446`

One parsed element. Purely local; the reader copies the fields it keeps.

### `static bool fits(const struct matter_tlv_reader *r, size_t off, size_t n)`
`modules/woz_matter/src/matter_tlv.c:462`

Test whether reading n bytes at offset off would stay within reader bounds.

**called by** `parse_at`, `scan_past_level_end`

### `static uint64_t read_le(const uint8_t *p, size_t n)`
`modules/woz_matter/src/matter_tlv.c:471`

Read an n-byte little-endian unsigned integer from buffer.
Returns the value as uint64_t; n must be in range [1, 8].

**called by** `matter_tlv_get_i64`, `matter_tlv_get_u64`, `parse_at`

### `static int parse_at(const struct matter_tlv_reader *r, size_t off, struct elem *e)`
`modules/woz_matter/src/matter_tlv.c:488`

Parse the element whose control byte is at @p off.
Does not follow container bodies: for a container, end == body_off and the
caller decides whether to enter or skip. That is what keeps this function
non-recursive.

**called by** `matter_tlv_next`, `scan_past_level_end`  ·  **calls** `fits`, `read_le`

### `static int scan_past_level_end(const struct matter_tlv_reader *r, size_t from, size_t *out)`
`modules/woz_matter/src/matter_tlv.c:590`

Walk forward from @p from to just past the end-of-container marker that
closes the level @p from sits in.
The nesting counter is the whole trick, and it is capped: deeply nested input
costs one loop iteration per element and never a stack frame.

**called by** `matter_tlv_exit`, `matter_tlv_next`  ·  **calls** `fits`, `parse_at`

### `void matter_tlv_reader_init(struct matter_tlv_reader *r, const uint8_t *buf, size_t len)`
`modules/woz_matter/src/matter_tlv.c:632`

Initialize a TLV reader to parse a buffer.
Sets reader to start-of-buffer state; if buf is NULL, len is set to zero and all reads will fail.

### `void matter_tlv_reader_set_implicit_profile(struct matter_tlv_reader *r, uint32_t profile)`
`modules/woz_matter/src/matter_tlv.c:647`

Set the implicit tag profile for subsequent context-tag (CTX) reads.
Allows the reader to interpret context tags in messages that omit the profile qualifier in the
wire format.

### `int matter_tlv_next(struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:664`

Load the next TLV element from the buffer, advancing the reader position.
Parses the control byte and tag at the current offset; skips end-of-container markers at top
level.
Returns MATTER_TLV_OK on success, MATTER_TLV_END when end-of-container marker is reached,
MATTER_TLV_E_TRUNC if buffer is incomplete, MATTER_TLV_E_TYPE or MATTER_TLV_E_INVAL on malformed
data.

**calls** `parse_at`, `scan_past_level_end`

### `matter_tlv_tag_t matter_tlv_tag(const struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:724`

Return the tag of the loaded TLV element.
Returns the tag value (profile-qualified or anonymous) or MATTER_TLV_ANON if no element loaded or
reader is NULL.

### `uint8_t matter_tlv_element_type(const struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:734`

Return the element type of the loaded TLV element.
Returns the numeric type code (MATTER_TLV_STRUCTURE, MATTER_TLV_ARRAY, etc.) or zero if no
element loaded or reader is NULL.

### `bool matter_tlv_is_container(const struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:743`

Test whether the loaded TLV element is a container (structure, array, or list).
Returns true if reader is not NULL, an element is loaded, and it is a container; false otherwise.

### `int matter_tlv_get_bool(const struct matter_tlv_reader *r, bool *out)`
`modules/woz_matter/src/matter_tlv.c:753`

Extract a boolean value from the current TLV element.
Returns MATTER_TLV_E_INVAL if reader or out is NULL; returns MATTER_TLV_E_STATE if no element
loaded; returns MATTER_TLV_E_TYPE if element is not a boolean type.

### `int matter_tlv_get_u64(const struct matter_tlv_reader *r, uint64_t *out)`
`modules/woz_matter/src/matter_tlv.c:772`

Decode an unsigned 64-bit integer from the current TLV element: read little-endian bytes. Returns
MATTER_TLV_E_TYPE if element type is not an unsigned integer (UINT8..UINT64).

**calls** `read_le`

### `int matter_tlv_get_i64(const struct matter_tlv_reader *r, int64_t *out)`
`modules/woz_matter/src/matter_tlv.c:792`

Decode a signed 64-bit integer from the current TLV element: read little-endian bytes and
sign-extend if shorter than 64 bits. Returns MATTER_TLV_E_TYPE if element type is not a signed
integer.

**calls** `read_le`

### `static int get_span(const struct matter_tlv_reader *r, uint8_t lo, uint8_t hi, const void **out, size_t *len)`
`modules/woz_matter/src/matter_tlv.c:830`

Extract a byte or UTF-8 span from the current TLV element.
Validates element type matches the allowed range [lo, hi], returns pointer into buffer and byte
count.
Returns MATTER_TLV_E_INVAL if out or len is NULL; returns MATTER_TLV_E_STATE if no element
loaded; returns MATTER_TLV_E_TYPE if element type is outside range.

**called by** `matter_tlv_get_bytes`, `matter_tlv_get_utf8`

### `int matter_tlv_get_bytes(const struct matter_tlv_reader *r, const uint8_t **out, size_t *len)`
`modules/woz_matter/src/matter_tlv.c:851`

Extract a byte or UTF-8 span from the current TLV element. Returns pointer and length in output
parameters; returns MATTER_TLV_E_TYPE if element is not a byte or UTF-8 span.

**calls** `get_span`

### `int matter_tlv_get_utf8(const struct matter_tlv_reader *r, const char **out, size_t *len)`
`modules/woz_matter/src/matter_tlv.c:860`

Extract a UTF-8 string from the current TLV element. Returns pointer and length in output
parameters; returns MATTER_TLV_E_TYPE if element is not a UTF-8 span.

**calls** `get_span`

### `int matter_tlv_enter(struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:871`

Descend one level into the current TLV container element.
Prepares the reader to iterate elements within the container body.
Returns MATTER_TLV_E_INVAL if reader is NULL; returns MATTER_TLV_E_STATE if no container is
loaded; returns MATTER_TLV_E_DEPTH if nesting exceeds MATTER_TLV_MAX_DEPTH.

### `int matter_tlv_exit(struct matter_tlv_reader *r)`
`modules/woz_matter/src/matter_tlv.c:894`

Ascend one level out of a TLV container.
Skips any unread elements within the current level and steps past the end-of-container marker.
Returns MATTER_TLV_E_INVAL if reader is NULL; returns MATTER_TLV_E_STATE if at top level; returns
MATTER_TLV_E_TRUNC if container is not properly closed.

**calls** `scan_past_level_end`

### `int matter_tlv_put_encoded(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const uint8_t *elem, size_t len)`
`modules/woz_matter/src/matter_tlv.c:935`

Re-tag a context-tagged TLV element by substituting its tag byte: input element must be
context-tagged (tag form 0xE0 bits set) and both source and destination tags must be context
tags. Single-byte substitution without re-encoding value or length. Returns MATTER_TLV_E_INVAL if
input is not a valid context-tagged element or destination tag is out of range.

**calls** `tag_number`, `tag_profile`
