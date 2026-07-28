<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/woz_uwb_facade.c`

UWB facade: binds the CCC credential-based STS engine to the DW3000 radio, exposes Aliro DS-TWR
responder start/stop and range query, and manages platform dependencies (HFCLK boost, SPI init,
callbacks).

**depends on** [`modules/woz_uwb/src/ccc/aliro_kdf.h`](../modules.woz_uwb.src.ccc/aliro_kdf.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](../modules.woz_uwb.src.ccc/ccc_shim.h.md), [`modules/woz_uwb/src/facade/flight_recorder.h`](flight_recorder.h.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.h`](woz_uwb_facade.h.md), [`modules/woz_uwb/src/fira/fira_session.h`](../modules.woz_uwb.src.fira/fira_session.h.md)  ·  **discussed in** [`docs/porting.md`](../../porting.md), [`web-twin/README.md`](../../../web-twin/README.md)

## API

### `static int woz_hfclk_boost(void)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:34`

@brief Raise the app-core HFCLK to 128 MHz before any driver initialises.
The app core boots with HFCLK divided to 64 MHz. What matters here is the
timing of the boost, not the boost itself: this used to run lazily on the
first ranging call, changing the core clock domain underneath an already
configured and actively used SPIM4. Doing it at PRE_KERNEL_1 means every
driver is configured once, against a clock that then never moves.
Note what this is NOT for. spi_nrfx_spim only consults the divider to cap
max_freq, and only when the requested rate exceeds 16 MHz (spi_nrfx_spim.c:182).
The DW3000 is at 8 MHz, so that clause never fires and the divider has no
bearing on the SPI bus rate either way.

### `static void woz_hfclk_ensure_128mhz(void)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:49`

@brief One-shot boost of the app-core HFCLK to 128 MHz for the DW3000 SPI bus.
nRF5340-specific platform seam: the app core boots with HFCLK divided to
64 MHz. Other SoCs (e.g. ESP32-S3) clock their SPI controller independently,
so this compiles to a no-op there. See docs/porting.md.

**called by** `woz_uwb_bind_ursk`, `woz_uwb_prewarm`, `woz_uwb_start_aliro`

### `int woz_uwb_bind_ursk(const uint8_t *ursk, size_t ursk_len)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:62`

@brief Bind the CCC STS from the add-on-supplied plaintext URSK; returns 0 on success.
@param ursk Pointer to the URSK bytes.
@param ursk_len Length of the URSK.
@return 0 on success, -EINVAL if ursk is NULL.

**calls** `woz_hfclk_ensure_128mhz`

### `int woz_uwb_start_aliro(const struct woz_uwb_aliro_cfg *c)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:77`

@brief Start the CCC DS-TWR responder bound to a live Aliro credential; returns 0 on success.
@param c Configuration struct (channel, sync_code_index, ursk, ranging_config, sts_index0,
slot_per_round).
@return 0 on success, -EINVAL if config is NULL or ursk is NULL, -EIO if radio initialization
fails.

**calls** `woz_hfclk_ensure_128mhz`

### `int woz_uwb_prewarm(uint8_t channel, uint8_t sync_code_index)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:119`

@brief Pre-apply the expected session PHY so the M4-time start skips dwt_configure.
@param channel UWB channel the upcoming session is expected to negotiate.
@param sync_code_index Expected SYNC code index.
@return 0 on success; the M4 start recovers with a full configure on any failure.

**calls** `woz_hfclk_ensure_128mhz`

### `void woz_uwb_stop(void)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:128`

@brief Quiesce the radio and unbind the CCC STS shim.

### `bool woz_uwb_last_range_cm(int32_t *cm_out)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:142`

@brief Retrieve the last valid DS-TWR distance measurement in centimeters.
@param cm_out Pointer to store the distance in cm.
@return True if a valid range has been seen since initialization; false otherwise.

### `void woz_uwb_set_range_listener(void (*cb)(void))`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:160`

@brief Register a callback fired after each accepted DS-TWR range latch.
@param cb Callback invoked on the UWB RX path (keep it to a task wake), or NULL to clear.
Without CONFIG_WOZ_ALIRO there is no range latch to observe and this is a no-op.

### `bool woz_uwb_trusted_range_cm(int32_t *cm_out)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:169`

Latest distance in cm, gated by the range-integrity consensus (layer 4):
true only when a valid range has been seen AND it is trusted
(fira_session_range_trusted()). This is the accessor the unlock decision
must use so a single unverified/spoofed block cannot drive an unlock; raw
telemetry keeps using woz_uwb_last_range_cm(). Without CONFIG_WOZ_ALIRO
there is no trust concept and this matches woz_uwb_last_range_cm().

**called by** `woz_uwb_trusted_range_after_cm`  ·  **calls** `woz_uwb_trusted_range_age_cm`

### `bool woz_uwb_trusted_range_age_cm(int32_t *cm_out, int64_t *age_ms_out)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:174`

As woz_uwb_trusted_range_cm(), plus how long ago that range landed. For
callers that must judge whether a range is still CURRENT rather than merely
the most recent one seen -- a distance from two minutes ago says nothing
about who is standing here now. Polling this beats registering a range
listener to timestamp latches: there is only one listener slot, and an app
that already owns it (the lock's approach loop) would otherwise be displaced.

**called by** `woz_uwb_trusted_range_cm`

### `uint32_t woz_uwb_range_generation(void)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:187`

Monotonic accepted-range epoch for post-challenge freshness checkpoints.

### `bool woz_uwb_trusted_range_after_cm(int32_t *cm_out, uint32_t after)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:196`

Trusted distance only when its accepted-range epoch is newer than @p after.
This is the demand-driven presence seam: an old latch can never satisfy a
challenge merely because it remains recent in wall-clock terms.

**called by** `woz_uwb_trusted_range_after_checked_cm`  ·  **calls** `woz_uwb_trusted_range_cm`

### `bool woz_uwb_trusted_range_after_checked_cm(int32_t *cm_out, uint32_t after, struct woz_uwb_range_integrity *ig_out)`
`modules/woz_uwb/src/facade/woz_uwb_facade.c:210`

As woz_uwb_trusted_range_after_cm(), plus the integrity evidence recorded
with that latch. The plain accessor answers "how far", which is all an unlock
decision needs; this one also answers "how well was that measured", which is
what a caller has to know before signing the number into a statement someone
else will believe. Without CONFIG_WOZ_ALIRO there is no evidence to report
and @p ig_out reads back as a failed STS.

**calls** `woz_uwb_trusted_range_after_cm`
