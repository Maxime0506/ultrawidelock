<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/ccc/ccc_shim_wrap.c`

@file ccc_shim_wrap.c — per-frame STS interception: woz_uwb_set_sts_iv() substitutes the CCC STS
for the FiRa MAC; target only.

**depends on** [`modules/woz_port/include/woz_log.h`](../modules.woz_port.include/woz_log.h.md), [`modules/woz_uwb/src/ccc/ccc_shim.h`](ccc_shim.h.md), [`modules/woz_uwb/src/driver/uwb_seam.h`](../modules.woz_uwb.src.driver/uwb_seam.h.md), [`modules/woz_uwb/src/facade/woz_bytes.h`](../modules.woz_uwb.src.facade/woz_bytes.h.md)  ·  **discussed in** [`docs/porting-esp32.md`](../../porting-esp32.md), [`docs/porting.md`](../../porting.md)

## API

### `void ccc_shim_wrap_log_reset(void)`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:41`

@brief Reset the frame logging counter to zero, used for re-enabling diagnostics after the first
N frames.

### `static void pack_key(dwt_sts_cp_key_t *out, const uint8_t dursk[CCC_DURSK_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:47`

@brief Pack a 16-byte `dURSK` into the DW3000 STS-key register image.

**called by** `woz_uwb_set_sts_iv`

### `static void pack_iv(dwt_sts_cp_iv_t *out, const uint8_t sts_v[CCC_STS_V_LEN])`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:66`

@brief Pack a 16-byte STS-V into the DW3000 STS-IV register image (whole-16 reverse then per-word
LE).
@param out DW3000 STS-IV register structure (iv0, iv1, iv2, iv3).
@param sts_v 16-byte STS-V value.

**called by** `woz_uwb_set_sts_iv`

### `void woz_uwb_set_sts_iv(dwt_sts_cp_iv_t *pStsIv)`
`modules/woz_uwb/src/ccc/ccc_shim_wrap.c:84`

@brief Intercept DW3000 STS IV configuration, deriving dURSK and STS-V from CCC secrets and
configuring the radio; falls through to the plain decadriver load if the shim is inactive.
@param pStsIv DW3000 STS-IV register structure (also holds the MAC's own frame index).

**calls** `pack_iv`, `pack_key`
