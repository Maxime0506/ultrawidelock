<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_shim.c`

@file ccc_shim.c — CCC STS substitution core (implementation).

**depends on** [`modules/woz_uwb/src/ccc/ccc_shim.h`](ccc_shim.h.md)

## API

### `static uint32_t ccc_shim_slot_from_sub(uint32_t sub)`
`modules/woz_uwb/src/ccc/ccc_shim.c:42`

@brief Map a blob sub-block offset to a CCC slot (currently pass-through).
@param sub Sub-block offset.
@return CCC slot index.

**called by** `ccc_shim_blob_to_ccc_index`

### `int ccc_shim_bind(const uint8_t mursk[CCC_MURSK_LEN], const uint8_t salted_hash[CCC_SALTED_HASH_LEN], uint32_t sts_index0, uint16_t n_slot_per_round)`
`modules/woz_uwb/src/ccc/ccc_shim.c:55`

@brief Bind the shim to a ranging session's derived key material.
@param mursk mURSK bytes.
@param salted_hash SaltedHash bytes.
@param sts_index0 Initial STS index for this session.
@param n_slot_per_round Slots per ranging cycle.
@return 0 on success; -EINVAL if any input is invalid.

**called by** `ccc_shim_bind_from_ursk`

### `int ccc_shim_bind_from_ursk(const uint8_t ursk[CCC_URSK_LEN], const uint8_t *ranging_config, size_t rc_len, uint32_t sts_index0, uint16_t n_slot_per_round)`
`modules/woz_uwb/src/ccc/ccc_shim.c:82`

@brief Bind the shim, deriving mURSK and SaltedHash from URSK and RangingConfiguration.
@param ursk 256-bit URSK input key.
@param ranging_config Serialized ranging configuration bytes (may be NULL if rc_len is 0).
@param rc_len Length of ranging_config in bytes.
@param sts_index0 Initial STS index for this session.
@param n_slot_per_round Slots per ranging cycle.
@return 0 on success; propagated error from derivation otherwise.

**calls** `ccc_shim_bind`

### `void ccc_shim_unbind(void)`
`modules/woz_uwb/src/ccc/ccc_shim.c:103`

@brief Unbind the shim; @ref ccc_shim_active returns false afterward.

### `bool ccc_shim_active(void)`
`modules/woz_uwb/src/ccc/ccc_shim.c:108`

@brief Whether the per-frame STS interception is live (bound AND not suspended).

### `uint32_t ccc_shim_sts_index0(void)`
`modules/woz_uwb/src/ccc/ccc_shim.c:114`

@brief The bound session's `STS_Index0` (for UAD/Pre-POLL derivation); 0 if unbound.

### `void ccc_shim_suspend(bool suspend)`
`modules/woz_uwb/src/ccc/ccc_shim.c:123`

@brief Suspend or resume the per-frame IV wrap without unbinding.
@param suspend True to suspend, false to resume.

### `int ccc_shim_sts_for_index(uint32_t sts_index, uint8_t dursk[CCC_DURSK_LEN], uint8_t sts_v[CCC_STS_V_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim.c:135`

@brief Map a per-frame STS index to its CCC dURSK (per-cycle) and STS-V (per-PPDU).
@param sts_index STS index in the ranging schedule.
@param dursk Output buffer receiving dURSK.
@param sts_v Output buffer receiving STS-V.
@return 0 on success; -EINVAL if shim is not active or output pointers are NULL.

**called by** `ccc_shim_sts_for_slot`

### `int ccc_shim_dudsk_for_index(uint32_t sts_index, uint8_t dudsk[CCC_DUDSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim.c:175`

@brief Derive dUDSK (per-cycle Final_Data key) for the ranging cycle containing sts_index.
@param sts_index STS index in the ranging schedule.
@param dudsk Output buffer receiving dUDSK.
@return 0 on success; -EINVAL if shim is not active or dudsk is NULL.

### `int ccc_shim_sts_for_slot(uint32_t slot, uint8_t dursk[CCC_DURSK_LEN], uint8_t sts_v[CCC_STS_V_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim.c:212`

@brief Map a ranging-slot offset (STS_Index0 plus slot) to its CCC dURSK and STS-V.
@param slot Slot offset from STS_Index0.
@param dursk Output buffer receiving dURSK.
@param sts_v Output buffer receiving STS-V.
@return 0 on success; -EINVAL if shim is not active.

**calls** `ccc_shim_sts_for_index`

### `uint32_t ccc_shim_index_from_iv(const uint8_t iv16[16])`
`modules/woz_uwb/src/ccc/ccc_shim.c:227`

@brief Extract the STS index from a DW3000 STS IV (index at bytes 7..4).
@param iv16 16-byte STS IV.
@return STS index.

### `uint32_t ccc_shim_blob_to_ccc_index(uint32_t blob_idx, uint32_t *block, uint32_t *sub)`
`modules/woz_uwb/src/ccc/ccc_shim.c:242`

@brief Map the blob's raw provisioned STS index to a CCC-schedule STS index, with origin and
stride auto-calibrated from the first two indices.
@param blob_idx Blob provisioned STS index.
@param block Output pointer receiving the ranging-cycle block index (may be NULL).
@param sub Output pointer receiving the intra-block sub-offset (may be NULL).
@return CCC-space STS index.

**calls** `ccc_shim_slot_from_sub`

### `void ccc_shim_pin_index(uint32_t ccc_index)`
`modules/woz_uwb/src/ccc/ccc_shim.c:288`

@brief Pin the substituted STS to one fixed CCC index (bench validation).
@param ccc_index CCC index to pin for all subsequent frames.

### `void ccc_shim_unpin(void)`
`modules/woz_uwb/src/ccc/ccc_shim.c:294`

@brief Release the debug pin; STS resumes advancing with the blob index.
