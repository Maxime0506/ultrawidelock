// Presence dongle commands (CONFIG_ULTRAWIDELOCK_PRESENCE): fresh, challenge-driven signed
// statements from a new trusted Aliro authentication and later UWB range, turning
// proximity of a provisioned iPhone into a factor any tool can check. See
// and for the other end.
//
// These are console commands rather than a private binary channel, so the shell
// stays available on the same board: provisioning (aliro-import) and presence both
// work without reflashing between modes. Every response is one tagged hex line, so
// a log line landing mid-conversation is just another line rather than corruption:
//
//   presence pub                 -> PRESENCE-PUB <65 bytes hex>   (enrolment)
//   presence credential          -> PRESENCE-CRED <8 bytes hex>   (pinned human)
//   presence prove <nonce-hex>   -> PRESENCE-P256 <115 bytes hex> (fresh proof)
//   anything rejected            -> PRESENCE-ERR <reason>
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generate or load the device signing key. Call once after the reader is up.
 *
 * drive_wallet_grant: send the phone the Reader-Status-Changed grant/relock as the
 * presence verdict changes. Pass false from any app that already drives that from
 * its own lock state, or the two owners will contradict each other. */
void presence_link_init(bool drive_wallet_grant);

/*
 * Require a new credential authentication and a later trusted UWB range.
 * Returns 0 only when the single provisioned credential ranges within policy.
 * Concurrent requests are serialized; no previous authentication or range can
 * authorize the caller.
 */
int presence_link_require_fresh(void);

/* Console handler for the `presence` command; registered by the app shell. */
int presence_link_cmd(int argc, char **argv);

#ifdef __cplusplus
}
#endif
