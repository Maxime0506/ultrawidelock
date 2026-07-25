// Presence dongle commands (CONFIG_WOZ_PRESENCE): signed statements of the current
// trusted-range presence state (aliro_assert), turning proximity of a provisioned
// iPhone into a factor any tool can check. See tools/presence_verify.py and
// tools/presence_git.py for the other end.
//
// These are console commands rather than a private binary channel, so the shell
// stays available on the same board: provisioning (aliro-import) and presence both
// work without reflashing between modes. Every response is one tagged hex line, so
// a log line landing mid-conversation is just another line rather than corruption:
//
//   presence pub                 -> PRESENCE-PUB <65 bytes hex>     (enrolment)
//   presence assert <nonce-hex>  -> PRESENCE-P256 <111 bytes hex>   (any verifier)
//   presence hmac <nonce-hex>    -> PRESENCE-HMAC <79 bytes hex>    (paired host)
//   presence key <32-byte hex>   -> PRESENCE-KEY ok                 (pairing key)
//   anything rejected            -> PRESENCE-ERR <reason>
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the range-latch listener, load the pairing key, and generate or load the
 * device signing key. Call once after the reader is up. */
void presence_link_init(void);

/* Persist a 32-byte pairing key to NVS and use it immediately. 0 on success,
 * negative on an NVS error. Also reachable as `presence key <hex>`. */
int presence_link_set_key(const uint8_t key[32]);

/* Console handler for the `presence` command; registered by the app shell. */
int presence_link_cmd(int argc, char **argv);

#ifdef __cplusplus
}
#endif
