<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/include/matter_tlv.h`

@file matter_tlv.h — Matter TLV codec (Matter Core spec, Appendix A).
This is NOT the BER/DER-TLV in modules/woz_aliro_stack/src/protocol/tlv.h.
Matter uses its own encoding: one control byte carrying a 3-bit tag control
and a 5-bit element type, then 0-8 tag octets, then the value, all
little-endian. The two share a name and nothing else, so they stay separate.

**depends on** [`modules/woz_matter/include/matter_status.h`](matter_status.h.md)  ·  **used by** [`modules/woz_matter/include/matter_im.h`](matter_im.h.md), [`modules/woz_matter/src/matter_attest.c`](../modules.woz_matter.src/matter_attest.c.md), [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md), [`modules/woz_matter/src/matter_fabric.c`](../modules.woz_matter.src/matter_fabric.c.md), [`modules/woz_matter/src/matter_pase.c`](../modules.woz_matter.src/matter_pase.c.md), [`modules/woz_matter/src/matter_tlv.c`](../modules.woz_matter.src/matter_tlv.c.md)

## API

### `struct matter_tlv_writer`
`modules/woz_matter/include/matter_tlv.h:106`

Encoder state. Errors are STICKY: the first failure is latched into rc and
every later put becomes a no-op, so a long encode sequence is checked once at
matter_tlv_writer_finish() instead of after every call. That is the shape
that keeps call sites readable, and it cannot silently truncate -- finish()
returns the latched error.

### `struct matter_tlv_reader`
`modules/woz_matter/include/matter_tlv.h:187`

---------------------------------------------------------------- decoder ---
Every byte here arrives from a peer, so the decoder's job is as much refusal
as decoding. Two properties it must hold, and both are structural rather
than checked:
1. NO RECURSION. Skipping an unentered container walks forward with a
nesting counter capped at MATTER_TLV_MAX_DEPTH. A recursive-descent
skip would let a peer choose this firmware's stack depth, on a part
where the system work queue was measured with 528 B to spare.
2. NO COPYING. Strings and octet strings are returned as a pointer into
the caller's buffer, so decoding allocates nothing and cannot truncate.
The pointer is valid exactly as long as that buffer is.
Iteration is CHIP-shaped because the shape is right: next() moves along the
current level and steps OVER a container it was not told to enter; enter()
descends; exit() skips whatever is left of the current container and lands
just past its end marker.

<details><summary>Undocumented (1)</summary>

- `matter_tlv_tag_t`

</details>
