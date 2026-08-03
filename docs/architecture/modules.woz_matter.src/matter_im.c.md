<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_im.c`

*No module docstring. First commit: "woz_matter: the Interaction Model, as far as a commissioner needs it".*

**depends on** [`modules/woz_matter/include/matter_im.h`](../modules.woz_matter.include/matter_im.h.md)

## API

### `static int decode_path(struct matter_tlv_reader *r, struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:86`

Decode one AttributePathIB. The reader is positioned ON the list element.

**called by** `matter_im_read_request_decode`, `matter_im_subscribe_request_decode`, `matter_im_write_request_decode`

### `int matter_im_read_request_decode(const uint8_t *tlv, size_t len, struct matter_im_read *out)`
`modules/woz_matter/src/matter_im.c:147`

Decode a Matter read request message from TLV to extract attribute paths and filter settings.
Parses ReadRequestMessage structure to collect attribute paths and fabric_filtered flag.
Returns MATTER_E_INVAL if tlv or out is NULL; returns MATTER_E_TYPE if element types are wrong;
returns MATTER_E_NOSPACE if path count exceeds MATTER_IM_MAX_PATHS.

**calls** `decode_path`

### `static void put_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:228`

Write one AttributePathIB. Concrete by construction: only reported paths.

**called by** `matter_im_write_response_encode`, `put_report`, `put_status_report`

### `static void put_status_report(struct matter_tlv_writer *w, const struct matter_im_path *p, uint8_t status)`
`modules/woz_matter/src/matter_im.c:244`

Append one AttributeReportIB carrying a status rather than a value.
Errors are not checked here because the writer latches the first one and
makes every later call a no-op; matter_tlv_writer_finish() reports it once.

**called by** `put_report`  ·  **calls** `put_path`

### `static void put_report(struct matter_tlv_writer *w, const struct matter_im_server *srv, const struct matter_im_path *p)`
`modules/woz_matter/src/matter_im.c:268`

Append one AttributeReportIB for a CONCRETE path: the value, or the status
saying why not.
The status is settled before a byte is committed. Deciding afterwards would
mean unwinding a half-written information block, and the writer latches its
first error and turns later calls into no-ops -- so an unwind would have to
reason about container depth that was never incremented.

**called by** `emit`  ·  **calls** `put_path`, `put_status_report`

### `struct chunk_ctx`
`modules/woz_matter/src/matter_im.c:303`

How far through a chunked report we are.
Threaded through the expansion instead of returned from it, because the
expansion is four nested loops and the alternative is a cursor into all four.

### `static void emit(struct matter_tlv_writer *w, const struct matter_im_server *srv, const struct matter_im_path *p, struct chunk_ctx *cc)`
`modules/woz_matter/src/matter_im.c:323`

Append one report unless this chunk is full or an earlier one carried it.
The write is attempted and ROLLED BACK when it does not fit, rather than
predicted: the size of a report depends on the value, and a prediction that
is wrong by one byte truncates a message that the peer cannot tell from a
complete one.

**called by** `expand_on_endpoint`, `report_encode`  ·  **calls** `put_report`

### `static void expand_on_endpoint(struct matter_tlv_writer *w, const struct matter_im_server *srv, const struct matter_im_path *p, struct matter_im_report_stats *stats, struct chunk_ctx *cc)`
`modules/woz_matter/src/matter_im.c:371`

Expand a wildcard attribute path by enumerating all attributes in the cluster if present.
If path specifies a single attribute, emit it; if attribute is unspecified, list all available
attributes and emit each.
Increments stats counters for unexpanded and skipped wildcard paths when attributes cannot be
listed or cluster is unavailable.

**called by** `report_encode`  ·  **calls** `emit`

### `static int report_encode(const struct matter_im_server *srv, const struct matter_im_read *req, uint8_t *out, size_t cap, size_t *out_len, struct matter_im_report_stats *stats, struct chunk_ctx *cc)`
`modules/woz_matter/src/matter_im.c:453`

Encode a Matter IM report containing attribute values for the requested read paths, expanding
wildcards by enumerating endpoints and clusters as needed. Track statistics on unexpanded
wildcards and set the more-chunks flag if the report was truncated. Return an error code.

**called by** `matter_im_report_data_chunk`, `matter_im_report_data_encode`  ·  **calls** `emit`, `expand_on_endpoint`

### `static int decode_command_path(struct matter_tlv_reader *r, struct matter_im_invoke *inv)`
`modules/woz_matter/src/matter_im.c:570`

Decode one CommandPathIB. The reader is positioned ON the list element.

**called by** `decode_command_data`

### `static int decode_command_data(struct matter_tlv_reader *r, struct matter_im_invoke *inv)`
`modules/woz_matter/src/matter_im.c:629`

Decode one CommandDataIB. The reader is positioned ON the structure.

**called by** `matter_im_invoke_request_decode`  ·  **calls** `decode_command_path`

### `int matter_im_invoke_request_decode(const uint8_t *tlv, size_t len, struct matter_im_invoke *out)`
`modules/woz_matter/src/matter_im.c:715`

Decode a Matter invoke request message from TLV to extract command path, arguments, and flags.
Parses InvokeRequestMessage structure: SuppressResponse, TimedRequest booleans and exactly one
CommandDataIB from the Requests array.
Returns MATTER_E_INVAL if tlv or out is NULL, if structure is missing, or if request count is not
exactly one; returns MATTER_E_NOSPACE if more than one command in array; returns MATTER_E_TYPE if
element types are wrong.

**calls** `decode_command_data`

### `static void put_command_path(struct matter_tlv_writer *w, matter_tlv_tag_t tag, const struct matter_im_invoke *inv, uint32_t command)`
`modules/woz_matter/src/matter_im.c:799`

Write one CommandPathIB naming @p command on the invoked path.

**called by** `matter_im_invoke_response_encode`

### `int matter_im_invoke_response_encode(const struct matter_im_server *srv, const struct matter_im_invoke *inv, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_im.c:817`

Encode a Matter invoke response message from command execution result and request parameters.
Runs the command immediately regardless of SuppressResponse flag; if suppress flag is set,
returns empty response.
Returns the command response on success, or status code on failure or unsupported attribute.
Returns MATTER_E_INVAL if srv, inv, or out is NULL; returns encoder error codes if TLV encoding
fails.

**calls** `put_command_path`

### `int matter_im_status_response_encode(uint8_t status, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_im.c:890`

Encode a Matter status response message with generic status code and revision.
Wraps status code in TLV structure suitable for error replies to read, write, or invoke requests.
Returns MATTER_E_INVAL if out or out_len is NULL; returns encoder error if TLV encoding fails.

### `int matter_im_timed_request_decode(const uint8_t *buf, size_t len, uint16_t *timeout_ms)`
`modules/woz_matter/src/matter_im.c:915`

Decode a Matter timed request message to extract timeout value in milliseconds.
Parses TimedRequestMessage structure for TimeoutMs field, clamped to uint16_t range.
Returns MATTER_E_INVAL if buf, timeout_ms is NULL, or len is zero; returns parser errors on
malformed TLV.

### `int matter_im_write_request_decode(const uint8_t *tlv, size_t len, struct matter_im_write *out)`
`modules/woz_matter/src/matter_im.c:968`

Decode a Matter write request message from TLV to extract attribute path, value, and flags.
Parses WriteRequestMessage: exactly one AttributeDataIB containing path and data value,
SuppressResponse and TimedRequest booleans.
Caps at one attribute per write; sets truncated flag if peer sends more, allowing response to
report RESOURCE_EXHAUSTED.
Returns MATTER_E_INVAL if tlv or out is NULL, if structure is missing, or if no attributes
present; returns MATTER_E_NOSPACE when path is incomplete; returns parser errors on malformed
TLV.

**calls** `decode_path`

### `int matter_im_write_response_encode(const struct matter_im_server *srv, const struct matter_im_write *wr, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_im.c:1123`

Encode a Matter write response message from write operation result.
Runs write immediately regardless of SuppressResponse flag; if suppress flag is set, returns
empty response.
Returns attribute status (success or error) for the written path.
Returns MATTER_E_INVAL if srv, wr, or out is NULL; returns encoder error codes if TLV encoding
fails.

**calls** `put_path`

### `int matter_im_subscribe_request_decode(const uint8_t *tlv, size_t len, struct matter_im_subscribe *out)`
`modules/woz_matter/src/matter_im.c:1186`

Decode a Matter subscription request message from TLV to extract attribute paths and timing
parameters.
Parses SubscribeRequestMessage: attribute paths, min/max interval timers (in seconds),
keep_subscriptions and fabric_filtered flags.
Returns MATTER_E_INVAL if tlv or out is NULL; returns MATTER_E_TYPE if element types are wrong;
returns MATTER_E_NOSPACE if path count exceeds MATTER_IM_MAX_PATHS.

**calls** `decode_path`

### `int matter_im_subscribe_response_encode(uint32_t subscription_id, uint16_t max_interval_s, uint8_t *out, size_t cap, size_t *out_len)`
`modules/woz_matter/src/matter_im.c:1276`

Encode a Matter subscription response message with subscription ID and maximum reporting
interval.
Wraps subscription details in TLV structure as initial reply to successful SubscribeRequest.
Returns MATTER_E_INVAL if out or out_len is NULL; returns encoder error if TLV encoding fails.

<details><summary>Undocumented (2)</summary>

- `matter_im_report_data_chunk` — tested: matter im
- `matter_im_report_data_encode` — tested: matter im

</details>
