/**
 * @file spakefake.c — host stand-in for the one oberon SPAKE2+ call our glue makes.
 *
 * matter_spake2p.c calls exactly one of the four oberon primitives:
 * ocrypto_spake2p_p256_reduce(). The other three are elliptic-curve operations
 * that the glue never invokes, so they are not defined here and any host test
 * that reached for them would fail to link -- which is the correct outcome,
 * since a fake curve would prove nothing.
 *
 * The reduction is not a fake. It is a real "40-byte big-endian integer modulo
 * the P-256 group order", done by shift-and-subtract so that no bignum library
 * is needed, and pinned in tests/host/test_matter_spake2p.c against values
 * python computed independently. Oberon's constant-time version is what runs on
 * target; this one only has to agree with it.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The order of the P-256 base point, big-endian (SEC 2, secp256r1 n). */
static const uint8_t k_order[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17,
	0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51,
};

/** @return true when the 32-byte big-endian @p a is >= the group order. */
static int ge_order(const uint8_t a[32])
{
	for (size_t i = 0; i < 32u; i++) {
		if (a[i] != k_order[i]) {
			return a[i] > k_order[i];
		}
	}
	return 1; /* equal counts as >= */
}

/** a -= order, big-endian, assuming a >= order. */
static void sub_order(uint8_t a[32])
{
	int borrow = 0;

	for (size_t i = 32u; i-- > 0;) {
		int d = (int)a[i] - (int)k_order[i] - borrow;

		if (d < 0) {
			d += 256;
			borrow = 1;
		} else {
			borrow = 0;
		}
		a[i] = (uint8_t)d;
	}
}

void ocrypto_spake2p_p256_reduce(uint8_t x[32], const uint8_t *xs, size_t xs_len)
{
	uint8_t r[32];

	memset(r, 0, sizeof(r));

	/* Shift the input in one bit at a time, reducing whenever it fits. Slow
	 * and obviously correct, which is what a reference wants to be. */
	for (size_t byte = 0; byte < xs_len; byte++) {
		for (int bit = 7; bit >= 0; bit--) {
			int carry = (xs[byte] >> bit) & 1;

			for (size_t i = 32u; i-- > 0;) {
				int v = (r[i] << 1) | carry;

				carry = (v >> 8) & 1;
				r[i] = (uint8_t)v;
			}
			if (carry != 0 || ge_order(r)) {
				sub_order(r);
			}
		}
	}

	memcpy(x, r, 32u);
}
