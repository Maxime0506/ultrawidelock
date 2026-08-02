/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * aliro_reader — Aliro reader session/transaction layer. Owns the per-connection
 * Aliro transaction on top of the aliro_ble transport: session lifecycle, the
 * credential-auth exchange (AUTH0/AUTH1/EXCHANGE), the reader identity and
 * credential trust gate, the M1-M4 ranging setup, and the handoff of the derived
 * URSK plus negotiated ranging parameters to the UWB engine.
 *
 * Crypto lives in the aliro_crypto component; the wire codec in aliro_apdu.c and
 * the ranging setup in aliro_ranging.c. See README.md for the flow and
 * components/aliro_ble/SPEC.md for the transport contract.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the Aliro reader (starts the BLE transport + session layer).
 *  Returns 0 on success, negative on failure. */
int aliro_reader_start(void);

/* ---- Attach mode: coexist with a host another stack owns (esp-matter) ----- *
 * Two phases so the reader shares one BLE controller with Matter:
 * aliro_reader_ble_prepare() runs BEFORE the host stack starts its GATT server
 * and returns the Aliro GATT service definition to register via that stack's
 * hook; aliro_reader_start_attached() runs once the host is up + the device is
 * operational (the owner has released the advertiser). */

/** Prepare the reader for attach mode + return the Aliro GATT service def to
 *  register (cast to `const struct ble_gatt_svc_def *`). NULL on failure. */
const void *aliro_reader_ble_prepare(void);

/** Start the reader on the shared host (L2CAP CoC + advertising + engine).
 *  Returns 0 on success, negative on failure. */
int aliro_reader_start_attached(void);

/** Re-emit the BLE advertisement using the currently-provisioned GRK. Call after
 *  Matter provisioning (SetAliroReaderConfig) if the reader may already be
 *  advertising: it starts on kCommissioningComplete, before Apple sends the Aliro
 *  config, so its first advertisement has no GRK and the phone cannot resolve it.
 *  No-op if the reader has no GRK or is not yet advertising. */
void aliro_reader_refresh_adv(void);

/* Observe the lock state the reader has just ANNOUNCED to the phone: true when
 * the grant that fires the Wallet animation goes out, false when the walk-away
 * relock does.
 *
 * Distinct from the access listener above, which reports a credential verdict at
 * authentication time. That fires on the unlock and never on the relock, so a
 * Matter tile driven from it would show a lock that opens and never closes.
 *
 * Called from the BLE-host task the moment the notification is sent, so the
 * listener must return immediately and must not block. NULL to unregister.
 * Unconditional, unlike the access hook: this fires twice per unlock rather than
 * on the transaction path, so a NULL check is not worth a Kconfig. */
void aliro_reader_set_lock_state_listener(void (*cb)(bool unlocked));

/* Feed one BLE connection-RSSI sample (dBm) into the session's ranging power gate
 * (CONFIG_WOZ_RSSI_GATE; absent without it). The transport polls the controller
 * every CONFIG_WOZ_RSSI_GATE_POLL_MS while its CoC is up and calls this from the
 * BLE-host task. The gate holds AP-Completed — and with it the whole UWB radio —
 * until the phone is inside the open threshold, and tears ranging down again on a
 * sustained fade below the close threshold. */
void aliro_reader_rssi_sample(uint16_t conn_handle, int8_t rssi_dbm);

/* Send the phone a "Reader Status Changed" SDU (Aliro transaction step 23) over the
 * active ranging session's BleSK channel: `unsecured` true on an approach grant (this
 * is what fires the iPhone Wallet unlock animation), false on relock. Safe to call
 * from any task -- it marshals the send onto the BLE-host task. No-op if no ranging
 * session is established. */
void aliro_reader_notify_unlock(bool unsecured);

/* Drives the one deferred piece of the above: a Secured that could not be delivered
 * because the peer had already gone is held for a few seconds after the next session
 * establishes, so that a phone which merely woke on the doorstep is not shown a lock
 * its own grant undoes a second later. Call from any periodic loop with a monotonic
 * millisecond clock; cheap enough to call unconditionally, and a no-op unless a
 * replay is pending. Nothing else needs it -- an ordinary walk-up never arms one. */
void aliro_reader_status_tick(int64_t now_ms);

/* True while some peer holds an established Aliro session (auth done, ranging
 * channel up). This is the reader's presence signal, and it is the one an approach
 * controller should relock on: ranging silence is not a departure, because iOS
 * pauses ranging when the phone stops moving (bench: 3.07 s with the phone 26 cm
 * from the reader). The link ending is a departure, and the RSSI gate's close path
 * is what ends the link when the peer walks out of range. Safe to call from any
 * task -- a plain read of the session table, no lock needed for a boolean. */
bool aliro_reader_session_active(void);

/* Register a listener for the per-transaction access verdict: true once a
 * credential has authenticated and passed the trust gate (including the
 * expedited-fast path and the dev-identity accept), false when one was presented
 * and rejected. This is the same decision the nRF5340's vendor application prints
 * as ACCESS GRANTED / ACCESS DENIED, and it is deliberately credential-independent
 * — the listener gets a verdict and nothing else, so an observer of it can never
 * leak a credential identifier.
 *
 * Called from the BLE-host task inside the transaction, alongside the software
 * P-256 work, so the listener must return immediately and must not block. Pass NULL
 * to unregister.
 *
 * Present only under CONFIG_WOZ_ALIRO_ACCESS_LISTENER; without it the hook and its
 * three notify points compile away entirely. */
#if defined(CONFIG_WOZ_ALIRO_ACCESS_LISTENER)
void aliro_reader_set_access_listener(void (*cb)(bool granted));
#endif

/* Copy out the credential public key (uncompressed P-256, 65 bytes) of the most
 * recent session that passed the trust check. The Matter door lock resolves it to
 * the user that owns it, so the LockOperation event names who unlocked; without
 * that the event is anonymous and Apple Home notifies every device in the home,
 * including the one that just unlocked. Returns true if a credential has
 * authenticated since boot (cred_pub written), false otherwise (left untouched).
 * Safe to call from any task. */
bool aliro_reader_authenticated_credential(uint8_t cred_pub[65]);

/* ---- Demand-driven presence proof --------------------------------------- *
 * A proof must not reuse the credential/range latches from a prior walk-up.
 * restart() marshals a disconnect of every current Aliro link onto the BLE
 * host task and returns a nonzero request ticket. checkpoint() becomes true
 * only after those links are gone; its auth_generation is the floor a new
 * transaction must advance past. */
uint32_t aliro_reader_presence_restart(void);
bool aliro_reader_presence_checkpoint(uint32_t request, uint32_t *auth_generation);

/* Copy the credential accepted by an authentication newer than checkpoint.
 * Returns false until a new trusted transaction has authenticated. */
bool aliro_reader_presence_authenticated_after(uint32_t checkpoint, uint8_t cred_pub[65]);

/* Presence is a named-human primitive, so ambiguity fails closed: returns one
 * pinned credential only when the provisioned trust store has exactly one
 * entry. Dev-open and multi-credential readers return false. */
bool aliro_reader_presence_expected_credential(uint8_t cred_pub[65]);

/* ---- Bench provisioning helpers (Phase 3.4) ---------------------------- *
 * Back the `aliro-prov` / `aliro-trust` console commands. Kept as plain calls
 * so the shell does not need the internal aliro_prov types. */

/** Print the reader identity (dev vs provisioned, reader_id), the trust store,
 *  and the most-recently-presented credential key. */
void aliro_reader_prov_print(void);

/** Trust the most-recently-presented credential public key and persist it to
 *  NVS. Returns 0 (added + saved), 1 (nothing presented yet, or already
 *  trusted), negative on a store error. */
int aliro_reader_trust_last(void);

/** Empty the trust store and persist it, keeping the reader identity. Returns 0
 *  (cleared + saved), 1 (already empty), negative on an NVS error. Needed because
 *  nothing evicts superseded credentials and a Matter factory reset leaves this
 *  store intact, so it otherwise fills up and rejects the current credential. */
int aliro_reader_trust_clear(void);

/* ---- Matter provisioning bridge (Phase 4) ------------------------------ *
 * Apple Home provisions the reader over Matter (Door Lock SetAliroReaderConfig +
 * SetCredential). These let the Matter delegate persist that identity + trust
 * into the same NVS store the reader loads at start(), so a handoff-started
 * reader authenticates the Wallet credential Apple just installed. Kept as plain
 * calls (no aliro_prov types) so the C++ delegate needs only this header. */

/** Store the reader identity provisioned over Matter and persist it to NVS:
 *  reader_id = groupIdentifier(16) || groupSubIdentifier(16),
 *  sign_priv = signingKey(32), grk = groupResolvingKey(16) for the BLE-UWB
 *  advertising dynamic tag (pass all-zero if none); clears the dev flag. Existing
 *  trust anchors are preserved. Returns 0 on success, negative on an NVS error. */
int aliro_reader_provision_identity(const uint8_t reader_id[32], const uint8_t sign_priv[32],
				    const uint8_t grk[16]);

/** Add a trusted credential public key (uncompressed P-256, 65 bytes) presented
 *  over Matter SetCredential and persist. Returns 0 (added), 1 (already
 *  present), negative (store full / not a P-256 point / NVS error). */
int aliro_reader_provision_add_trust(const uint8_t cred_pub[65]);

/** Revert to the dev identity + empty trust store (Matter ClearAliroReaderConfig)
 *  and persist. Returns 0 on success, negative on an NVS error. */
int aliro_reader_provision_clear(void);

/* ---- Identity clone (bench, CONFIG_WOZ_ALIRO_CLONE) --------------------- *
 * Replicate a reader's identity + trust store onto a second board so a phone's
 * existing credential transacts with the clone (the "no pairing dance" path for
 * the presence-second-factor experiment). export_blob emits the reader private
 * key, so only the clone-gated console commands reach these. */

/** Serialise the current identity + trust store into a portable blob (backs the
 *  `aliro-export` console command). Returns 0 and sets *out_len; -1 if cap is
 *  too small. The blob contains the reader private key. */
int aliro_reader_export_blob(uint8_t *out, size_t cap, size_t *out_len);

/** Adopt an identity + trust store from an exported blob, persist it, and use it
 *  live (backs `aliro-import`). 0 ok; -1 malformed blob; -2 NVS write failed. */
int aliro_reader_import_blob(const uint8_t *buf, size_t len);

/* ---- Step-up (Access Document) bench control (CONFIG_WOZ_ALIRO_STEPUP) ---- *
 * Back the `aliro-stepup` console command. Both are no-ops unless the reader was
 * built with the step-up phase enabled. */

/** Arm a one-shot Access-Document request: the next transaction is forced into
 *  the standard phase and requests + verifies a document. Never per-unlock; the
 *  verdict is logged only and never gates access. */
void aliro_reader_stepup_arm(void);

/** Print the armed state and the most recent verification verdict. */
void aliro_reader_stepup_status(void);

#ifdef __cplusplus
}
#endif
