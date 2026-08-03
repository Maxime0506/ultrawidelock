<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_im.h`

@file matter_im.h — the Interaction Model, as far as a commissioner needs it.
Once PASE is done the commissioner stops speaking Secure Channel and starts
asking questions. The first one is a ReadRequest, and until something answers
it the phone waits, gives up, and shows "pairing failed" with no further clue.
in    ReadRequest   (protocol 0x0001, opcode 0x02)
out   ReportData    (protocol 0x0001, opcode 0x05)
This is the read half only. Write, Invoke and Subscribe are separate messages
and land when something needs them; commissioning cannot start without Read.
It holds no device data. Which endpoints exist and what their attributes say
is matter_clusters.h's, reached through @ref matter_im_server, so the wire
format can be tested without a device and the device without a wire.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md), [`modules/woz_matter/include/matter_tlv.h`](matter_tlv.h.md)  ·  **used by** [`modules/woz_matter/include/matter_clusters.h`](matter_clusters.h.md), [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md), [`modules/woz_matter/src/matter_im.c`](../modules.woz_matter.src/matter_im.c.md)

## API

### `struct matter_im_path`
`modules/woz_matter/include/matter_im.h:119`

One requested path. An absent field is a WILDCARD, and that distinction
decides how a path that matches nothing is answered -- see
matter_im_report_data_encode().

### `static inline bool matter_im_path_is_wildcard(const struct matter_im_path *p)`
`modules/woz_matter/include/matter_im.h:129`

True when any component is wildcarded (app/AttributePathParams.h:57-59).

### `struct matter_im_read`
`modules/woz_matter/include/matter_im.h:135`

A decoded ReadRequest.

### `struct matter_im_subscribe`
`modules/woz_matter/include/matter_im.h:151`

A SubscribeRequest: the same paths a read asks for, plus how often.

### `struct matter_im_invoke`
`modules/woz_matter/include/matter_im.h:256`

One command a commissioner asked this node to run.

### `struct matter_im_server`
`modules/woz_matter/include/matter_im.h:353`

Callback table for the Matter interaction model server, defining functions to query attribute
status, read values, list endpoints and clusters, and handle write and command operations.

### `struct matter_im_write`
`modules/woz_matter/include/matter_im.h:385`

One attribute write.
Exactly one per message, the same restriction matter_im_invoke has and for
the same reason: a commissioner that sent two and saw one status would be
entitled to assume both applied.

### `struct matter_im_report_stats`
`modules/woz_matter/include/matter_im.h:448`

What encoding a report had to leave out. Worth logging; none of it is fatal.
