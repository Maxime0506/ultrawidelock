<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/fira/fira_session.c`

@file fira_session.c — Range + URSK store for the CCC Pre-POLL responder.

**depends on** [`modules/woz_port/include/woz_port.h`](../modules.woz_port.include/woz_port.h.md), [`modules/woz_uwb/src/ccc/aliro_kdf.h`](../modules.woz_uwb.src.ccc/aliro_kdf.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](fira_session.h.md)  ·  **discussed in** [`bot/README.md`](../../../bot/README.md), [`docs/porting-esp32.md`](../../porting-esp32.md), [`web-twin/README.md`](../../../web-twin/README.md)

## API

### `void fira_session_set_provisioned_ursk(const uint8_t *ursk)`
`modules/woz_uwb/src/fira/fira_session.c:18`

@brief Stash an Aliro URSK for the CCC Pre-POLL STS decode; NULL clears it.

### `const uint8_t *fira_session_get_ursk(void)`
`modules/woz_uwb/src/fira/fira_session.c:29`

@brief The stashed Aliro URSK (32 bytes), or NULL if none — for the Pre-POLL decode.

### `uint32_t fira_session_current_slot(void)`
`modules/woz_uwb/src/fira/fira_session.c:34`

@brief STS-index slot clock (inert without a MAC time base); returns 0.

### `bool fira_session_last_range(int32_t *cm_out, uint16_t *addr_out, uint8_t *nlos_out, uint32_t *block_out, int64_t *age_ms_out)`
`modules/woz_uwb/src/fira/fira_session.c:76`

@brief Fetch the most recent valid DS-TWR range; out-params optional (NULL to skip).

### `bool fira_session_range_plausible(int32_t cm)`
`modules/woz_uwb/src/fira/fira_session.c:101`

@brief Layer 1: true if @p cm is a physically plausible DS-TWR distance.

**called by** `fira_session_set_ccc_range_cm`

### `bool fira_session_sts_quality_ok(int32_t driver_verdict, int16_t quality_index)`
`modules/woz_uwb/src/fira/fira_session.c:106`

@brief Layer 2: true if the STS correlated well enough to trust its timestamp.
@param driver_verdict  dwt_readstsquality() return (>=0 good, <0 bad).
@param quality_index   the signed STS quality index it wrote.

**called by** `fira_session_set_ccc_range_sts`

### `bool fira_session_range_trusted(void)`
`modules/woz_uwb/src/fira/fira_session.c:111`

@brief Layer 4: true once >= K consecutive plausible, mutually consistent
ranges have been latched. Cleared by any implausible or outlier block.

### `uint8_t fira_session_trust_level(void)`
`modules/woz_uwb/src/fira/fira_session.c:116`

@brief Layer 4 diagnostic: the live run length of agreeing plausible blocks
(0..FIRA_RANGE_TRUST_K) behind fira_session_range_trusted().

### `void fira_session_reset_ranges(void)`
`modules/woz_uwb/src/fira/fira_session.c:121`

@brief Invalidate the old session's range and consensus before a new URSK
session starts. The monotonic generation is retained so callers can prove
that a later latch happened after their checkpoint.

### `void fira_session_set_ccc_range_sts(int32_t driver_verdict, int16_t quality_index)`
`modules/woz_uwb/src/fira/fira_session.c:130`

@brief Record the layer-2 STS evidence for the block that is about to latch.
Separate from the latch itself because the responder RX path owns the DW3000
diagnostics and the store does not. Call it immediately before
fira_session_set_ccc_range_cm(); the latch consumes the evidence and clears
it, so a latch with no preceding call records "no evidence", which reads as
a failed STS rather than a passed one.
@param driver_verdict dwt_readstsquality() return (>=0 good, <0 bad).
@param quality_index  the signed STS quality index it wrote.

**calls** `fira_session_sts_quality_ok`

### `bool fira_session_last_range_integrity(struct fira_range_integrity *out)`
`modules/woz_uwb/src/fira/fira_session.c:137`

@brief Read the integrity evidence for the latched range.
Reports the run, not the last block: a consumer that fails closed needs to
know that every block which built the consensus was well-correlated, since
an attacker who can land one good block among three suspect ones has not
been stopped by a check that only inspects the last.
@return false (leaving @p out untouched) when no range has been latched.

### `uint32_t fira_session_range_generation(void)`
`modules/woz_uwb/src/fira/fira_session.c:152`

@brief Monotonic generation incremented after every accepted range latch.

### `void fira_session_set_range_listener(void (*cb)(void))`
`modules/woz_uwb/src/fira/fira_session.c:157`

@brief Register a callback fired after each accepted range latch (NULL to
clear). Runs on the UWB RX path — keep it to a task wake, nothing heavier.

### `void fira_session_set_ccc_range_cm(int32_t cm, uint32_t block)`
`modules/woz_uwb/src/fira/fira_session.c:162`

@brief Latch a CCC DS-TWR range so it flows up the Aliro mRangingData seam.

**calls** `fira_session_range_plausible`
