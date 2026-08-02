<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_msg.c`

@file matter_msg.c — Matter message and protocol header codec.

**depends on** [`modules/woz_matter/include/matter_msg.h`](../modules.woz_matter.include/matter_msg.h.md)

## API

### `static size_t msg_header_len(uint8_t flags)`
`modules/woz_matter/src/matter_msg.c:61`

Header length implied by a message flags byte, or 0 if the byte is invalid.
Rejecting DSIZ 3 here rather than treating it as "no destination" matters: it
is reserved, so a peer using it is either broken or probing, and silently
accepting would put this decoder's idea of the payload offset out of step
with the sender's.

**called by** `matter_msg_header_decode`, `matter_msg_header_encode`

<details><summary>Undocumented (14)</summary>

- `rd16`
- `rd32`
- `rd64`
- `wr16`
- `wr32`
- `wr64`
- `proto_header_len`
- `matter_msg_is_secure` — tested: matter msg
- `matter_msg_header_decode` — tested: matter exchange; matter msg
- `matter_msg_header_encode` — tested: matter msg
- `matter_counter_init` — tested: matter msg
- `matter_counter_next` — tested: matter msg
- `matter_proto_header_decode` — tested: matter exchange; matter msg
- `matter_proto_header_encode` — tested: matter exchange; matter msg

</details>
