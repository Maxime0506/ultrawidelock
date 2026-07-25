<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/facade/woz_uwb_facade.h`

Public header for UWB facade: exposes Aliro DS-TWR responder lifecycle and range query; the CCC
engine is bound and unbound via internal ursk and stop calls.

**used by** [`modules/woz_aliro/src/aliro_ranging.c`](../modules.woz_aliro.src/aliro_ranging.c.md), [`modules/woz_uwb/src/ccc/cherry_ccc_shim.c`](../modules.woz_uwb.src.ccc/cherry_ccc_shim.c.md), [`modules/woz_uwb/src/driver/uwb_selftest.c`](../modules.woz_uwb.src.driver/uwb_selftest.c.md), [`modules/woz_uwb/src/facade/flight_recorder.c`](flight_recorder.c.md), [`modules/woz_uwb/src/facade/woz_uwb_facade.c`](woz_uwb_facade.c.md)  ·  **discussed in** [`docs/porting-esp32-phase3.md`](../../porting-esp32-phase3.md), [`docs/porting.md`](../../porting.md)

## API

### `struct woz_uwb_aliro_cfg`
`modules/woz_uwb/src/facade/woz_uwb_facade.h:35`

@brief Aliro UWB ranging parameters negotiated during M1-M4 handshake.
@param session_id Aliro UWB session identifier (any non-zero value).
@param channel UWB operating channel (5 or 9).
@param sync_code_index SYNC/preamble code index (1..32).
@param slot_duration_rstu Slot duration in RSTU units (1200 = 1 ms).
@param block_duration_ms Ranging block repetition period in milliseconds.
@param slot_per_round Number of slots per ranging round.
@param sts_index0 Starting STS (Scrambled Timestamp Sequence) index.
@param uwb_time_us UWB_Time0 initiation reference in microseconds.
@param ursk 32-byte URSK (provisioned STS root key).
@param ranging_config Serialized RangingConfiguration (CCC SaltedHash input), or NULL to use URSK
fallback.
@param rc_len RangingConfiguration length in bytes (typically 17).
