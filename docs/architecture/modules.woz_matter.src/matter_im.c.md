<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_im.c`

*No module docstring. First commit: "woz_matter: the Interaction Model, as far as a commissioner needs it".*

**depends on** [`modules/woz_matter/include/matter_im.h`](../modules.woz_matter.include/matter_im.h.md)

## API

### `static int decode_path(struct matter_tlv_reader *r, struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:86`

Decode one AttributePathIB. The reader is positioned ON the list element.

**called by** `matter_im_read_request_decode`, `matter_im_subscribe_request_decode`, `matter_im_write_request_decode`

### `static void put_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:222`

Write one AttributePathIB. Concrete by construction: only reported paths.

**called by** `matter_im_write_response_encode`, `put_report`, `put_status_report`

### `static void put_status_report(struct matter_tlv_writer *w, const struct matter_im_path *p, uint8_t status)`
`modules/woz_matter/src/matter_im.c:238`

Append one AttributeReportIB carrying a status rather than a value.
Errors are not checked here because the writer latches the first one and
makes every later call a no-op; matter_tlv_writer_finish() reports it once.

**called by** `put_report`  ·  **calls** `put_path`

### `static void put_report(struct matter_tlv_writer *w, const struct matter_im_server *srv, const struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:262`

Append one AttributeReportIB for a CONCRETE path: the value, or the status
saying why not.
The status is settled before a byte is committed. Deciding afterwards would
mean unwinding a half-written information block, and the writer latches its
first error and turns later calls into no-ops -- so an unwind would have to
reason about container depth that was never incremented.

**called by** `emit`  ·  **calls** `put_path`, `put_status_report`

### `struct chunk_ctx`
`modules/woz_matter/src/matter_im.c:297`

How far through a chunked report we are.
Threaded through the expansion instead of returned from it, because the
expansion is four nested loops and the alternative is a cursor into all four.

### `static void emit(struct matter_tlv_writer *w, const struct matter_im_server *srv, const struct matter_im_path *p, struct chunk_ctx *cc)`
`modules/woz_matter/src/matter_im.c:317`

Append one report unless this chunk is full or an earlier one carried it.
The write is attempted and ROLLED BACK when it does not fit, rather than
predicted: the size of a report depends on the value, and a prediction that
is wrong by one byte truncates a message that the peer cannot tell from a
complete one.

**called by** `expand_on_endpoint`, `report_encode`  ·  **calls** `put_report`

### `static int decode_command_path(struct matter_tlv_reader *r, struct matter_im_invoke *inv)`
`modules/woz_matter/src/matter_im.c:552`

Decode one CommandPathIB. The reader is positioned ON the list element.

**called by** `decode_command_data`

### `static int decode_command_data(struct matter_tlv_reader *r, struct matter_im_invoke *inv)`
`modules/woz_matter/src/matter_im.c:611`

Decode one CommandDataIB. The reader is positioned ON the structure.

**called by** `matter_im_invoke_request_decode`  ·  **calls** `decode_command_path`

### `static void put_command_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const struct matter_im_invoke *inv, uint32_t command)`
`modules/woz_matter/src/matter_im.c:773`

Write one CommandPathIB naming @p command on the invoked path.

**called by** `matter_im_invoke_response_encode`

<details><summary>Undocumented (13)</summary>

- `matter_im_read_request_decode` — tested: matter im
- `expand_on_endpoint`
- `matter_im_report_data_chunk` — tested: matter im
- `matter_im_report_data_encode` — tested: matter im
- `report_encode`
- `matter_im_invoke_request_decode` — tested: matter im invoke
- `matter_im_invoke_response_encode` — tested: matter addnoc; matter im invoke; matter network
- `matter_im_status_response_encode` — tested: matter im
- `matter_im_timed_request_decode` — tested: matter im
- `matter_im_write_request_decode` — tested: matter im write
- `matter_im_write_response_encode` — tested: matter im write
- `matter_im_subscribe_request_decode` — tested: matter im write
- `matter_im_subscribe_response_encode`

</details>
