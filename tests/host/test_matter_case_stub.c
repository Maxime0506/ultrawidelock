/**
 * @file test_matter_case_stub.c — ECDH and signing the suite can steer.
 *
 * The host suite has no P-256, so these record what they were asked to do and
 * return something deterministic. That is enough for what IS checkable here:
 * which bytes get signed, which key signs them, and that the shared secret
 * reaches the salt. The signature itself is verified against the only judge
 * that matters, a real commissioner.
 */
/* Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include "test_matter_case_stub.h"

#include <string.h>

#include "matter_case.h"

uint8_t g_case_signed[1024];
size_t g_case_signed_len;
uint8_t g_case_sign_priv[32];
int g_case_sign_calls;
int g_case_ecdh_calls;
uint8_t g_case_ecdh_peer[MATTER_CASE_PUBKEY_LEN];
int g_case_ecdh_fail;
int g_case_sign_fail;

void test_matter_case_stub_reset(void)
{
	memset(g_case_signed, 0, sizeof(g_case_signed));
	g_case_signed_len = 0u;
	memset(g_case_sign_priv, 0, sizeof(g_case_sign_priv));
	g_case_sign_calls = 0;
	g_case_ecdh_calls = 0;
	memset(g_case_ecdh_peer, 0, sizeof(g_case_ecdh_peer));
	g_case_ecdh_fail = 0;
	g_case_sign_fail = 0;
}

int matter_case_ecdh(const uint8_t priv[32], const uint8_t peer_pub[MATTER_CASE_PUBKEY_LEN],
		     uint8_t secret_out[MATTER_CASE_SECRET_LEN])
{
	g_case_ecdh_calls++;
	if (g_case_ecdh_fail) {
		return -1;
	}
	memcpy(g_case_ecdh_peer, peer_pub, MATTER_CASE_PUBKEY_LEN);
	/* Deterministic and dependent on both inputs, so a test can tell a
	 * secret that was derived from a secret that was not. */
	for (size_t i = 0; i < MATTER_CASE_SECRET_LEN; i++) {
		secret_out[i] = (uint8_t)(priv[i] ^ peer_pub[i + 1u] ^ 0x5Au);
	}
	return 0;
}

int matter_case_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len,
		     uint8_t sig[MATTER_CASE_SIG_LEN])
{
	g_case_sign_calls++;
	if (g_case_sign_fail) {
		return -1;
	}
	if (msg_len > sizeof(g_case_signed)) {
		return -1;
	}
	memcpy(g_case_signed, msg, msg_len);
	g_case_signed_len = msg_len;
	memcpy(g_case_sign_priv, priv, sizeof(g_case_sign_priv));

	for (size_t i = 0; i < MATTER_CASE_SIG_LEN; i++) {
		sig[i] = (uint8_t)(0xA0u + i);
	}
	return 0;
}
