// Presence dongle protocol (CONFIG_WOZ_PRESENCE): serve a host challenge/response
// on the console UART. On each challenge the dongle answers with a signed
// statement of the current trusted-range presence state (aliro_assert), turning
// proximity of a provisioned iPhone into a host second factor. See
// host/presence/ for the other end.
//
// Wire frames, each led by 'A' so a desynchronised reader can resynchronise:
//   'A''C' + 16-byte nonce -> 79-byte HMAC assertion, verifiable only by the
//                             host holding the pairing key
//   'A''P' + 16-byte nonce -> 111-byte ECDSA-P256 assertion, verifiable by any
//                             holder of the device public key
//   'A''Q'                 -> 65-byte uncompressed P-256 public point (enrolment)
//   'A''K' + 32-byte key   -> load the pairing key, no response
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the range-latch listener and load the pairing key from NVS. Call once
 * after the reader is up, before presence_link_serve(). */
void presence_link_init(void);

/* Persist a 32-byte pairing key to NVS and use it immediately. 0 on success,
 * negative on an NVS error. Also invoked in-band by the 'A''K' key-load frame. */
int presence_link_set_key(const uint8_t key[32]);

/* Blocking: process challenge / key-load frames on the console UART forever.
 * Replaces the interactive REPL in presence mode. */
void presence_link_serve(void);

#ifdef __cplusplus
}
#endif
