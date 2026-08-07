<!-- generated documentation — edit the source, not this file -->
# `firmware/src/matter_commission.c`

@file matter_commission.c — joins BTP, the exchange and PASE.
Three finished pieces and no protocol of its own:
matter_ble_zephyr.c   bytes in and out over the 0xFFF6 service
matter_exchange.c     which session, which exchange, duplicate, ack
matter_pase_sm.c      the five commissioning messages
What is left for this file is the wiring nobody else can do: pulling the
SPAKE2+ verifier out of configuration, drawing real randomness, and deciding
what happens when a commissioner disappears halfway through.

**depends on** [`firmware/src/matter_ble_zephyr.h`](matter_ble_zephyr.h.md), [`firmware/src/matter_commission.h`](matter_commission.h.md), [`firmware/src/matter_fab_settings.h`](matter_fab_settings.h.md), [`firmware/src/status_led.h`](status_led.h.md)  ·  **discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `int matter_attest_ecdsa_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len, uint8_t sig[MATTER_ATTEST_SIG_LEN])`
`firmware/src/matter_commission.c:179`

The two seams matter_attest.h declares. Kept here rather than in the module
so woz_matter stays free of any particular crypto backend; on this board both
are the reader's existing PSA-backed primitives.

### `int matter_attest_ec_keygen(uint8_t priv[32], uint8_t pub[65])`
`firmware/src/matter_commission.c:189`

Generate a P-256 keypair for Matter attestation. Fills priv with the 32-byte private key and pub
with the 65-byte uncompressed public key. Returns 0 on success.

### `int matter_case_ecdh(const uint8_t priv[32], const uint8_t peer_pub[MATTER_CASE_PUBKEY_LEN], uint8_t secret_out[MATTER_CASE_SECRET_LEN])`
`firmware/src/matter_commission.c:200`

The two matter_case.h declares. ECDH yields the X coordinate only, which is
what the spec means by the shared secret -- the Y coordinate carries no
additional entropy and including it would give a secret neither peer agrees
on.

### `int matter_case_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len, uint8_t sig[MATTER_CASE_SIG_LEN])`
`firmware/src/matter_commission.c:210`

Sign a message with a P-256 private key. Fills sig with the MATTER_CASE_SIG_LEN-byte signature.
Returns 0 on success.

### `int matter_case_verify(const uint8_t pub[MATTER_CASE_PUBKEY_LEN], const uint8_t *msg, size_t msg_len, const uint8_t sig[MATTER_CASE_SIG_LEN])`
`firmware/src/matter_commission.c:220`

Verify a message signature with a P-256 public key. The public key is MATTER_CASE_PUBKEY_LEN
bytes, the signature is MATTER_CASE_SIG_LEN bytes. Returns 0 if the signature is valid.

### `static int unhex(const char *s, uint8_t *out, size_t cap, size_t *len)`
`firmware/src/matter_commission.c:440`

@return 0 and the byte count, or -EINVAL on any non-hex or odd-length input.

**called by** `load_verifier`

### `static int load_verifier(void)`
`firmware/src/matter_commission.c:472`

Read the verifier out of Kconfig.
A verifier and the parameters that produced it have to agree, and nothing on
this device can check that they do -- a mismatched pair fails at Pake3 with
cA wrong and no way to tell that apart from a wrong passcode. So this checks
the shapes it can and says so loudly when they are wrong, which is the only
warning anyone gets.

**called by** `matter_commission_init`  ·  **calls** `unhex`

### `static void admin_close(void)`
`firmware/src/matter_commission.c:531`

Close the Matter commissioning window if open. Reset the verifier to the factory default, clear
the window state, fabric index, and vendor code, set the BLE discriminator to 0, re-advertise,
and log the closure. If the window is not open, return silently.

**called by** `admin_expire`, `admin_revoke`

### `static void admin_expire(struct k_work *work)`
`firmware/src/matter_commission.c:548`

Work callback that closes the Matter commissioning window when the timeout expires.

**calls** `admin_close`

### `static void admin_arm(uint16_t timeout_s, uint8_t kind)`
`firmware/src/matter_commission.c:561`

Open the Matter commissioning window for the specified kind (basic or enhanced) and timeout in
seconds. Set the administrative window state, reschedule the admin timer, re-advertise on BLE,
and if CONFIG_WOZ_DFU_RECEIVER is enabled, open the DFU update window for the same timeout
(converted to milliseconds). Log the window opening.

**called by** `admin_open_basic`, `admin_open_enhanced`

### `static uint8_t admin_open_enhanced(uint16_t timeout_s, const uint8_t *verifier, uint32_t verifier_len, uint16_t discriminator, uint32_t iterations, const uint8_t *salt, uint32_t salt_len)`
`firmware/src/matter_commission.c:582`

Open the Matter commissioning window with an enhanced PAKE verifier, new discriminator, and the
specified timeout in seconds. Validates verifier length, point format (0x04 prefix), salt length,
and iterations; returns MATTER_ADMIN_STATUS_PAKE_PARAM_ERROR if any are invalid. Returns
MATTER_ADMIN_STATUS_BUSY if a window is already open, otherwise returns 0u.

**calls** `admin_arm`

### `static uint8_t admin_open_basic(uint16_t timeout_s)`
`firmware/src/matter_commission.c:617`

Open the Matter commissioning window with the factory PAKE verifier and the specified timeout in
seconds. Returns MATTER_ADMIN_STATUS_BUSY if a window is already open, otherwise returns 0u.

**calls** `admin_arm`

### `static uint8_t admin_revoke(void)`
`firmware/src/matter_commission.c:633`

Close the Matter commissioning window if one is open. Returns MATTER_ADMIN_STATUS_WINDOW_NOT_OPEN
if already closed, otherwise returns 0u.

**calls** `admin_close`

### `static uint8_t admin_status(void)`
`firmware/src/matter_commission.c:646`

Return the administrative window state: one of the MATTER_ADMIN_WINDOW_* constants.

### `bool matter_commission_window_open(void)`
`firmware/src/matter_commission.c:651`

True while an AdministratorCommissioning window is open.
The advertiser needs this: a node that HAS a fabric normally advertises as
an Aliro reader, and doing that during a commissioning window hides it from
the very ecosystem the window was opened for.

### `static uint8_t admin_fabric(void)`
`firmware/src/matter_commission.c:660`

Return the fabric index of the peer commissioning the lock, or 0 if no commissioning is in
progress.

### `static uint16_t admin_vendor(void)`
`firmware/src/matter_commission.c:669`

Return the vendor code offered by the peer during commissioning, or 0 if no commissioning is in
progress.

### `static int begin_session(void)`
`firmware/src/matter_commission.c:684`

Fresh randomness for one commissioning attempt.

**called by** `on_message`

### `static uint8_t case_slot_of(uint16_t session_id)`
`firmware/src/matter_commission.c:795`

The slot holding @p session_id, or MATTER_CASE_SESSIONS if none does.

**called by** `handle_sigma3`, `matter_thread_on_datagram`, `notify_lock_state`, `on_status_response`

### `static uint8_t case_alloc_slot(void)`
`firmware/src/matter_commission.c:815`

A slot for a newly established session: a free one, else the round-robin
victim.
Evicting is a real loss -- whoever held that session goes silent with no way
to be told -- so it happens only once there are more administrators than
slots, and it is logged.

**called by** `handle_sigma3`  ·  **calls** `sub_drop_session`

### `static void send_framed(uint8_t opcode, const uint8_t *payload, size_t len)`
`firmware/src/matter_commission.c:853`

Frame and send a Matter message with the specified opcode and payload. Over BLE, send via
matter_ble_send; over Thread, stage the framed bytes in s_thread_reply. Log errors if framing
fails or the buffer is too small.

**called by** `on_message`

### `static void fab_store_work_fn(struct k_work *w)`
`firmware/src/matter_commission.c:931`

Work function to persist the operational Matter fabric identity to settings storage. Retry
FAB_STORE_ATTEMPTS times with FAB_STORE_BACKOFF_MS delay between retries. If successful, reset
the attempt counter. If all retries fail, log an error that the fabric was not stored and the
node will come back commissionable on the next boot, then reset the attempt counter.

### `static void send_im(uint8_t opcode, const uint8_t *payload, size_t len)`
`firmware/src/matter_commission.c:968`

Frame and send a Matter Interaction Model message with the specified opcode and payload. Over
BLE, send via matter_ble_send; over Thread, stage the framed bytes in s_thread_reply. Log errors
if framing fails or the buffer is too small.

**called by** `on_invoke_request`, `on_read_request`, `on_status_response`, `on_timed_request`, `on_write_request`, `send_report_chunk`

### `static void on_read_request(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:1014`

Handle an incoming Matter ReadRequest. Decodes the paths being read, logs them per session type
(loud over CASE only), and builds a ReportData response. Warns if any wildcard paths could not be
expanded.

**called by** `on_secure`  ·  **calls** `send_im`

### `static void on_invoke_request(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:1074`

Handle an incoming Matter InvokeRequest. Decodes the request, builds an InvokeResponse, and on
successful Door Lock or Network Commissioning commands, submits a notification to trigger
subscription reports before sending. Stores fabrics and credentials to NVS only on
CommissioningComplete to avoid pairing delays and stack overflow on the receive path.

**called by** `on_secure`  ·  **calls** `notify_lock_state_changed`, `send_im`

### `static void on_write_request(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:1206`

Apply a WriteRequest.
The commissioner's last act, and the one this node used to answer with
silence: an ACL entry granting itself Administer over CASE. A home app that
has finished commissioning and cannot record that it owns the node sits on
"Adding to home" until it gives up.

**called by** `on_secure`  ·  **calls** `send_im`

### `struct sub_state`
`firmware/src/matter_commission.c:1247`

The subscriptions this node is serving.
One slot per session, because that is the natural bound: a controller
subscribes on the session it holds. This was a SINGLE subscription, on the
argument that Apple opens exactly one during commissioning and a table would
be RAM spent on a case that had not arrived. The case had arrived. Every
SubscribeRequest overwrote the last, so the displaced controller saw its
subscription stop, re-subscribed at once, and displaced the next one -- with
nothing in the log to say so, because each round looks like a healthy
subscribe. Measured on 2026-08-02: nine of these in five minutes and a tile
that never left "No Response".

### `struct sub_persist`
`firmware/src/matter_commission.c:1316`

Persisted subscription state: peer node ID, subscription ID, maximum heartbeat interval in
seconds, fabric index, and a used flag.

### `static void sub_persist_save(uint8_t slot, const struct sub_state *s, uint64_t peer_node, uint8_t fabric_index)`
`firmware/src/matter_commission.c:1343`

Persist one subscription's state to settings storage with the key SUB_KEY_FMT[slot]. Skip
persisting if peer_node or fabric_index is zero (no match key available). Log a warning if save
fails; the subscription will not survive reboot.

**called by** `on_status_response`

### `static int sub_persist_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)`
`firmware/src/matter_commission.c:1373`

Settings callback to load one persisted subscription from the settings key-value store. Reads up
to len bytes into *out if len matches the struct size. Returns 0.

### `static void sub_persist_load(void)`
`firmware/src/matter_commission.c:1387`

Load the stored records, dormant until a matching CASE session turns up.

**called by** `matter_commission_init`

### `static void sub_resume_for(uint8_t case_slot, uint64_t peer_node, uint8_t fabric_index, uint16_t session_id)`
`firmware/src/matter_commission.c:1413`

A CASE session just came up. If a stored subscription belongs to this peer on
this fabric, put it back to work on the new session.

**called by** `handle_sigma3`  ·  **calls** `subscription_heartbeat_arm`

### `static void notify_lock_state(struct sub_state *s)`
`firmware/src/matter_commission.c:1483`

Send a Matter lock state subscription report to one CASE session. Builds a TLV-encoded data
report for the DoorLock cluster LockState attribute and sends it as an initiator exchange. Logs
the subscription ID and byte counts. Returns silently if the session is not in use, active, and
valid.

**called by** `heartbeat_work_fn`, `notify_work_fn`  ·  **calls** `case_slot_of`

### `static void notify_work_fn(struct k_work *w)`
`firmware/src/matter_commission.c:1541`

Work callback that sends lock state subscription reports to all CASE sessions.

**calls** `notify_lock_state`

### `static void notify_lock_state_changed(void)`
`firmware/src/matter_commission.c:1563`

Submit lock state change notification to the work queue, and move the lock LED.
The LED is driven from here rather than from on_aliro_lock_state() because
this is the one point BOTH movers reach: a walk-up arrives through the Aliro
listener, and a Home tile tap arrives through the Door Lock cluster, which
writes s_info.lock_state itself and never touches that listener. Hanging the
light off the listener alone gave a board whose LED ignored the app.

**called by** `on_aliro_lock_state`, `on_invoke_request`

### `static uint32_t subscription_heartbeat_period_s(void)`
`firmware/src/matter_commission.c:1635`

Compute the heartbeat period in seconds for all active subscriptions. Returns the minimum of (3/4
* max_interval_s) across all subscriptions, or SUBSCRIPTION_HEARTBEAT_S if none are active,
floored to SUBSCRIPTION_HEARTBEAT_MIN_S.

**called by** `heartbeat_work_fn`, `subscription_heartbeat_arm`

### `static void heartbeat_work_fn(struct k_work *w)`
`firmware/src/matter_commission.c:1660`

Work callback that sends lock state subscription reports to all active CASE sessions and
reschedules itself only if at least one subscription remains active.

**calls** `notify_lock_state`, `subscription_heartbeat_period_s`

### `static void subscription_heartbeat_arm(void)`
`firmware/src/matter_commission.c:1683`

Schedule the subscription heartbeat work with the minimum period across all active subscriptions.

**called by** `on_status_response`, `sub_resume_for`  ·  **calls** `subscription_heartbeat_period_s`

### `static void on_aliro_lock_state(bool unlocked)`
`firmware/src/matter_commission.c:1700`

The Aliro side of this lock moved, so Matter has to be told.
A walk-up unlock and its walk-away relock never went through the Door Lock
cluster at all -- they are the reader's own transaction -- so LockState kept
whatever the last tile tap set it to. The Wallet animated "unlocked" while
the Home tile said locked, and the app was not wrong so much as uninformed:
nothing had reported the change.
Runs on the BLE-host task, so it does the cheapest possible thing: set a byte
and submit. The report itself is built on the system work queue.

**calls** `notify_lock_state_changed`

### `static uint16_t current_session_id(void)`
`firmware/src/matter_commission.c:1713`

The session serving the datagram in flight; 0 when it arrived over BLE.

**called by** `on_status_response`, `on_subscribe_request`

### `static struct sub_state *sub_of_session(uint16_t session_id)`
`firmware/src/matter_commission.c:1723`

The subscription @p session_id holds, or NULL.

**called by** `on_status_response`, `sub_alloc`, `sub_drop_session`

### `static void sub_drop_session(uint16_t session_id)`
`firmware/src/matter_commission.c:1737`

Mark the subscription holding session_id as no longer in use, or return silently if no
subscription holds that session ID.

**called by** `case_alloc_slot`  ·  **calls** `sub_of_session`

### `static struct sub_state *sub_alloc(uint16_t session_id)`
`firmware/src/matter_commission.c:1754`

The slot for a new subscription from @p session_id.
Re-subscribing on a session REPLACES what that session already had, rather
than taking a second slot: a controller that asks again has abandoned the
first, and letting one peer hold several is how a table of six starves at
two controllers -- the same failure this table exists to end.

**called by** `on_subscribe_request`  ·  **calls** `sub_of_session`

### `static void send_report_chunk(struct sub_state *s)`
`firmware/src/matter_commission.c:1782`

Send one chunk of the priming report.
The whole data model does not fit one Matter message -- the spec caps a
message at the IPv6 MTU and this node's answer measured 1479 bytes of payload
against a ~1232 byte ceiling. An oversized datagram is not slow, it is never
delivered, and the subscriber re-subscribes forever with nothing to say why.

**called by** `on_status_response`, `on_subscribe_request`  ·  **calls** `send_im`

### `static void on_subscribe_request(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:1824`

Begin a subscription.
The order is not the obvious one. A SubscribeRequest is answered with the
REPORT, not with the SubscribeResponse: the subscriber acknowledges that
report with a StatusResponse, and only then is the SubscribeResponse sent
(ReadHandler.cpp:240-250). Answering the request directly leaves the
subscriber holding an id for a subscription whose initial values never
arrived, which is indistinguishable from a node that stopped reporting.

**called by** `on_secure`  ·  **calls** `current_session_id`, `send_report_chunk`, `sub_alloc`

### `static int on_aliro_credential_clear(uint8_t credential_type, uint16_t credential_index)`
`firmware/src/matter_commission.c:2007`

Matter ClearCredential: stop honouring one Aliro credential, or every one of them.
An issuer key was never an anchor, so clearing one is already true and says so without touching
the store. Everything else resolves through the credential index the SetCredential recorded.
Returns 0 only when the removal is persisted, because the cluster turns anything else into a
FAILURE the admin can act on.

### `static int on_aliro_user_clear(uint16_t user_index)`
`firmware/src/matter_commission.c:2045`

Matter ClearUser: drop every Aliro credential bound to a user, or to all users.
The user row itself is the cluster's to forget; this is only the trust store half. Returns 0 only
when the removal is persisted.

### `static int on_aliro_reader_config(const uint8_t signing_key[32], const uint8_t verification_key[65], const uint8_t group_id[16], const uint8_t *group_resolving_key)`
`firmware/src/matter_commission.c:2062`

Complete Aliro reader provisioning from a Matter commissioning exchange. Store the reader
identity (derived from the group ID and group sub-ID) and the signing key into the Aliro reader
engine, retire the device key, and log success or error.

### `static void on_timed_request(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:2088`

Handle an incoming Matter TimedRequest. Decodes the timeout and answers with a StatusResponse of
SUCCESS.

**called by** `on_secure`  ·  **calls** `send_im`

### `static void on_status_response(const struct matter_exchange_in *in)`
`firmware/src/matter_commission.c:2112`

Handle a StatusResponse in a subscription priming sequence: send the next report chunk if more
remain, or finalize the subscription, persist it to settings storage, and arm periodic
heartbeats.

**called by** `on_secure`  ·  **calls** `case_slot_of`, `current_session_id`, `send_im`, `send_report_chunk`, `sub_of_session`, `sub_persist_save`, `subscription_heartbeat_arm`

### `static size_t send_sigma2(const struct matter_case_sigma1 *s1, const uint8_t *ipk, const uint8_t *sigma1, size_t sigma1_len, const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`firmware/src/matter_commission.c:2305`

Build and frame the Sigma2 answering @p s1.
@param sigma1 the Sigma1 payload EXACTLY as it arrived -- the transcript hash
is over those bytes, and rebuilding them would be rebuilding something
the peer hashed and this node did not.

**called by** `matter_thread_on_datagram`

### `static size_t handle_sigma3(const uint8_t *sigma3, size_t sigma3_len, const uint8_t *ipk, const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`firmware/src/matter_commission.c:2572`

Answer a Sigma3, which ends the handshake.
Sigma2 asked the initiator to believe this node; Sigma3 is the initiator
proving the same thing back, and it is the last message either side sends in
the clear. What follows it is encrypted under keys neither side transmitted,
so a mistake here surfaces as silence on the NEXT message rather than as a
failure on this one -- which is the reason for the checks logged below.

**called by** `matter_thread_on_datagram`  ·  **calls** `case_alloc_slot`, `case_slot_of`, `case_status_report`, `sub_resume_for`

### `static size_t case_status_report(const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`firmware/src/matter_commission.c:2755`

The StatusReport that ends CASE.
Still unsecured and still addressed to the initiator's ephemeral id: this is
the last message before the keys take effect, not the first one after.

**called by** `handle_sigma3`

### `size_t matter_thread_on_datagram(const uint8_t *msg, size_t len, uint8_t *reply, size_t cap)`
`firmware/src/matter_commission.c:2803`

A datagram on the operational port. Sigma1, so far, and only Sigma1.
There is no responder yet, so this answers nothing. What it does establish is
the thing that cannot be checked any other way: whether the identity the
initiator is asking for is THIS node's. The destination identifier is an HMAC
under the fabric's operational IPK, so recomputing it and finding a match
proves the whole chain -- AddNOC's IPK, the compressed fabric id derived from
the root key, the fabric and node ids out of the NOC -- all agree with what a
real commissioner computed independently.

**calls** `case_slot_of`, `handle_sigma3`, `on_secure`, `send_sigma2`

### `static void on_link_reset(void)`
`firmware/src/matter_commission.c:3109`

The link dropped. Cheap here; begin_session() does the real work later.

### `bool matter_commission_has_fabric(void)`
`firmware/src/matter_commission.c:3126`

Whether this node currently holds a commissioned Matter fabric.
Asked by the advertiser, which can carry the Matter commissionable
service data OR the Aliro reader tag but not both: flags 3 + Matter 12 +
Aliro 26 is 41 bytes in a 31-byte legacy packet, and a second advertising
set costs 24.8 KB of RAM.
A device with no fabric MUST stay commissionable. Provisioning the reader
identity used to flip the advert to Aliro on its own, which left a board
that had just been provisioned -- and had lost its fabric to a failed
pairing -- invisible to Add Accessory and impossible to recover without
erasing it.

### `int matter_commission_init(void)`
`firmware/src/matter_commission.c:3136`

Register the commissioning handlers on the 0xFFF6 transport.
Call after the reader is up. Nothing here touches the radio: whether the
board is discoverable as a commissionable node is decided by the advertising
branch in aliro_ble_zephyr.c, which asks for the Matter payload while this
node holds no fabric -- see matter_commission_has_fabric().
@return 0. A bad verifier is reported by log and refused per attempt rather
than failing startup -- a reader that cannot commission should still
be a reader.

**calls** `load_verifier`, `sub_persist_load`

<details><summary>Undocumented (3)</summary>

- `on_aliro_credential`
- `on_secure`
- `on_message`

</details>
