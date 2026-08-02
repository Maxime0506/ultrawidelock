<!-- generated documentation — edit the source, not this file -->
# `modules/woz_matter/src/matter_spake2p.c`

@file matter_spake2p.c — PBKDF2, the SPAKE2+ transcript and confirmations.

**depends on** [`modules/woz_aliro/src/aliro_hash.h`](../modules.woz_aliro.src/aliro_hash.h.md), [`modules/woz_matter/include/matter_spake2p.h`](../modules.woz_matter.include/matter_spake2p.h.md)

## API

### `static void tt_put(uint8_t *out, size_t *off, const uint8_t *data, size_t len)`
`modules/woz_matter/src/matter_spake2p.c:166`

Append one length-prefixed transcript element.

**called by** `matter_spake2p_transcript`

<details><summary>Undocumented (6)</summary>

- `matter_pbkdf2_sha256` — tested: matter spake2p
- `matter_spake2p_w0w1` — tested: matter spake2p
- `matter_spake2p_context` — tested: matter spake2p
- `matter_spake2p_transcript` — tested: matter spake2p
- `matter_spake2p_p2` — tested: matter spake2p
- `matter_spake2p_verify` — tested: matter spake2p

</details>
