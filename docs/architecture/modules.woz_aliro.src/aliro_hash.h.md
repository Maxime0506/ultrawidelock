<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro/src/aliro_hash.h`

Streaming SHA-256 (FIPS 180-4) implementation used by the Aliro crypto layer.
Declares struct aliro_sha256, the incremental hash context used across init/update/finish
calls.

**used by** [`modules/woz_aliro/src/aliro_assert.c`](aliro_assert.c.md), [`modules/woz_aliro/src/aliro_crypto.c`](aliro_crypto.c.md), [`modules/woz_aliro/src/aliro_hash.c`](aliro_hash.c.md), [`modules/woz_aliro/src/aliro_stepup.c`](aliro_stepup.c.md), [`modules/woz_matter/src/matter_case.c`](../modules.woz_matter.src/matter_case.c.md), [`modules/woz_matter/src/matter_crypto.c`](../modules.woz_matter.src/matter_crypto.c.md), [`modules/woz_matter/src/matter_fabric.c`](../modules.woz_matter.src/matter_fabric.c.md), [`modules/woz_matter/src/matter_spake2p.c`](../modules.woz_matter.src/matter_spake2p.c.md)

## API

### `struct aliro_sha256`
`modules/woz_aliro/src/aliro_hash.h:30`

Streaming SHA-256 (FIPS 180-4).
