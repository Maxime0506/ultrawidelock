/**
 * @file test_ccc_shim_wrap.c — the per-frame STS IV intercept (ccc_shim_wrap.c).
 *
 * On target __wrap_dwt_configurestsiv sits behind ld --wrap; on host the suite
 * calls the wrapper directly. The DW3000 side is the recording shim
 * (tests/host/shim/shim.c + dw_rx_stub.c): woz_host_last_sts_key/iv capture
 * what would hit the radio registers, and dwt_read_reg returns 0 — so this
 * pins the shim-active/inactive gating, the blob-index calibration handoff,
 * and the key/IV register packing (whole-16 reverse + per-word LE) against the
 * real CCC KDF, not any radio behaviour. The "KEY wr/rd" readback compare is
 * log-only and vacuous here (the fake registers read 0).
 */
#include <string.h>

#include "ccc_shim.h"
#include "deca_device_api.h"
#include "test.h"

/* The wrapper + reset under test (ccc_shim_wrap.c). */
void __wrap_dwt_configurestsiv(dwt_sts_cp_iv_t *pStsIv);

/* Capture state from tests/host/shim/shim.c. */
extern dwt_sts_cp_key_t woz_host_last_sts_key;
extern dwt_sts_cp_iv_t woz_host_last_sts_iv;

static const uint8_t URSK[CCC_URSK_LEN] = {
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
	0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
};
static const uint8_t RC[16] = {
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
#define STS0  0x00220000u
#define NSLOT 12u

/** Build the blob-side IV image: STS index little-endian at bytes 4..7. */
static dwt_sts_cp_iv_t blob_iv(uint32_t blob_idx)
{
	uint8_t b[16];
	dwt_sts_cp_iv_t iv;

	memset(b, 0xa5, sizeof(b));
	b[4] = (uint8_t)blob_idx;
	b[5] = (uint8_t)(blob_idx >> 8);
	b[6] = (uint8_t)(blob_idx >> 16);
	b[7] = (uint8_t)(blob_idx >> 24);
	memcpy(&iv, b, sizeof(iv));
	return iv;
}

/** Expected register word: 16-byte value reversed whole, then LE per word. */
static uint32_t regword(const uint8_t v16[16], int word)
{
	/* rev[i] = v16[15 - i]; word w reads rev[4w..4w+3] little-endian. */
	int base = 15 - 4 * word;

	return (uint32_t)v16[base] | ((uint32_t)v16[base - 1] << 8) |
	       ((uint32_t)v16[base - 2] << 16) | ((uint32_t)v16[base - 3] << 24);
}

void test_ccc_shim_wrap(void)
{
	uint8_t dursk[CCC_DURSK_LEN], sts_v[CCC_STS_V_LEN];
	dwt_sts_cp_iv_t iv;

	t_group("inactive: passthrough to the real IV load");
	ccc_shim_unbind();
	iv = blob_iv(0x11223344u);
	memset(&woz_host_last_sts_iv, 0, sizeof(woz_host_last_sts_iv));
	__wrap_dwt_configurestsiv(&iv);
	T_OK("iv reaches real load verbatim",
	     memcmp(&woz_host_last_sts_iv, &iv, sizeof(iv)) == 0);

	t_group("active: CCC STS substituted (frame 0 calibrates the origin)");
	T_EQ("bind", ccc_shim_bind_from_ursk(URSK, RC, sizeof(RC), STS0, NSLOT), 0);
	ccc_shim_wrap_log_reset();
	iv = blob_iv(0x0badc0deu); /* arbitrary blob origin */
	__wrap_dwt_configurestsiv(&iv);
	/* Independent expectation: frame 0 maps to CCC index STS0 exactly. */
	T_EQ("kdf ref", ccc_shim_sts_for_index(STS0, dursk, sts_v), 0);
	T_EQ("key0 packed", (long)woz_host_last_sts_key.key0, (long)regword(dursk, 0));
	T_EQ("key1 packed", (long)woz_host_last_sts_key.key1, (long)regword(dursk, 1));
	T_EQ("key2 packed", (long)woz_host_last_sts_key.key2, (long)regword(dursk, 2));
	T_EQ("key3 packed", (long)woz_host_last_sts_key.key3, (long)regword(dursk, 3));
	T_EQ("iv0 packed", (long)woz_host_last_sts_iv.iv0, (long)regword(sts_v, 0));
	T_EQ("iv1 packed", (long)woz_host_last_sts_iv.iv1, (long)regword(sts_v, 1));
	T_EQ("iv2 packed", (long)woz_host_last_sts_iv.iv2, (long)regword(sts_v, 2));
	T_EQ("iv3 packed", (long)woz_host_last_sts_iv.iv3, (long)regword(sts_v, 3));

	t_group("frame 1 learns the stride; block 1 advances one round");
	iv = blob_iv(0x0badc0deu + 96u);
	__wrap_dwt_configurestsiv(&iv);
	T_EQ("kdf ref blk1", ccc_shim_sts_for_index(STS0 + NSLOT, dursk, sts_v), 0);
	T_EQ("blk1 iv0", (long)woz_host_last_sts_iv.iv0, (long)regword(sts_v, 0));
	T_EQ("blk1 iv3", (long)woz_host_last_sts_iv.iv3, (long)regword(sts_v, 3));
	T_EQ("blk1 key0", (long)woz_host_last_sts_key.key0, (long)regword(dursk, 0));

	t_group("past the log budget: substitution continues silently");
	for (int i = 0; i < 10; i++) {
		iv = blob_iv(0x0badc0deu + 96u * (uint32_t)(2 + i));
		__wrap_dwt_configurestsiv(&iv);
	}
	T_EQ("kdf ref blk11", ccc_shim_sts_for_index(STS0 + 11u * NSLOT, dursk, sts_v), 0);
	T_EQ("blk11 iv0", (long)woz_host_last_sts_iv.iv0, (long)regword(sts_v, 0));

	t_group("suspended: passthrough again without unbinding");
	ccc_shim_suspend(true);
	iv = blob_iv(0x31415926u);
	__wrap_dwt_configurestsiv(&iv);
	T_OK("suspended iv verbatim", memcmp(&woz_host_last_sts_iv, &iv, sizeof(iv)) == 0);
	ccc_shim_suspend(false);

	t_group("NULL iv: forwarded, capture untouched");
	iv = woz_host_last_sts_iv;
	__wrap_dwt_configurestsiv(NULL);
	T_OK("null forwarded safely", memcmp(&woz_host_last_sts_iv, &iv, sizeof(iv)) == 0);

	ccc_shim_unbind(); /* leave no session behind for later suites */
}
