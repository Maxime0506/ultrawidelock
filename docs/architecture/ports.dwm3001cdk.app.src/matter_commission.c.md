<!-- generated documentation — edit the source, not this file -->
# `ports/dwm3001cdk/app/src/matter_commission.c`

@file matter_commission.c — joins BTP, the exchange and PASE.
Three finished pieces and no protocol of its own:
matter_ble_zephyr.c   bytes in and out over the 0xFFF6 service
matter_exchange.c     which session, which exchange, duplicate, ack
matter_pase_sm.c      the five commissioning messages
What is left for this file is the wiring nobody else can do: pulling the
SPAKE2+ verifier out of configuration, drawing real randomness, and deciding
what happens when a commissioner disappears halfway through.

**depends on** [`ports/dwm3001cdk/app/src/matter_ble_zephyr.h`](matter_ble_zephyr.h.md), [`ports/dwm3001cdk/app/src/matter_commission.h`](matter_commission.h.md), [`ports/dwm3001cdk/app/src/matter_fab_settings.h`](matter_fab_settings.h.md)

## API

### `int matter_attest_ecdsa_sign(const uint8_t priv[32], const uint8_t *msg, size_t msg_len, uint8_t sig[MATTER_ATTEST_SIG_LEN])`
`ports/dwm3001cdk/app/src/matter_commission.c:158`

The two seams matter_attest.h declares. Kept here rather than in the module
so woz_matter stays free of any particular crypto backend; on this board both
are the reader's existing PSA-backed primitives.

### `int matter_case_ecdh(const uint8_t priv[32], const uint8_t peer_pub[MATTER_CASE_PUBKEY_LEN], uint8_t secret_out[MATTER_CASE_SECRET_LEN])`
`ports/dwm3001cdk/app/src/matter_commission.c:175`

The two matter_case.h declares. ECDH yields the X coordinate only, which is
what the spec means by the shared secret -- the Y coordinate carries no
additional entropy and including it would give a secret neither peer agrees
on.

### `static int unhex(const char *s, uint8_t *out, size_t cap, size_t *len)`
`ports/dwm3001cdk/app/src/matter_commission.c:257`

@return 0 and the byte count, or -EINVAL on any non-hex or odd-length input.

**called by** `load_verifier`

### `static int load_verifier(void)`
`ports/dwm3001cdk/app/src/matter_commission.c:289`

Read the verifier out of Kconfig.
A verifier and the parameters that produced it have to agree, and nothing on
this device can check that they do -- a mismatched pair fails at Pake3 with
cA wrong and no way to tell that apart from a wrong passcode. So this checks
the shapes it can and says so loudly when they are wrong, which is the only
warning anyone gets.

**called by** `matter_commission_init`  ·  **calls** `unhex`

### `static int begin_session(void)`
`ports/dwm3001cdk/app/src/matter_commission.c:328`

Fresh randomness for one commissioning attempt.

**called by** `on_message`

### `static uint8_t case_slot_of(uint16_t session_id)`
`ports/dwm3001cdk/app/src/matter_commission.c:439`

The slot holding @p session_id, or MATTER_CASE_SESSIONS if none does.

**called by** `handle_sigma3`, `matter_thread_on_datagram`, `notify_lock_state`

### `static uint8_t case_alloc_slot(void)`
`ports/dwm3001cdk/app/src/matter_commission.c:459`

A slot for a newly established session: a free one, else the round-robin
victim.
Evicting is a real loss -- whoever held that session goes silent with no way
to be told -- so it happens only once there are more administrators than
slots, and it is logged.

**called by** `handle_sigma3`  ·  **calls** `sub_drop_session`

### `static void on_write_request(const struct matter_exchange_in *in)`
`ports/dwm3001cdk/app/src/matter_commission.c:779`

Apply a WriteRequest.
The commissioner's last act, and the one this node used to answer with
silence: an ACL entry granting itself Administer over CASE. A home app that
has finished commissioning and cannot record that it owns the node sits on
"Adding to home" until it gives up.

**called by** `on_secure`  ·  **calls** `send_im`

### `struct sub_state`
`ports/dwm3001cdk/app/src/matter_commission.c:820`

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

### `static void on_aliro_lock_state(bool unlocked)`
`ports/dwm3001cdk/app/src/matter_commission.c:997`

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
`ports/dwm3001cdk/app/src/matter_commission.c:1010`

The session serving the datagram in flight; 0 when it arrived over BLE.

**called by** `on_status_response`, `on_subscribe_request`

### `static struct sub_state *sub_of_session(uint16_t session_id)`
`ports/dwm3001cdk/app/src/matter_commission.c:1020`

The subscription @p session_id holds, or NULL.

**called by** `on_status_response`, `sub_alloc`, `sub_drop_session`

### `static struct sub_state *sub_alloc(uint16_t session_id)`
`ports/dwm3001cdk/app/src/matter_commission.c:1047`

The slot for a new subscription from @p session_id.
Re-subscribing on a session REPLACES what that session already had, rather
than taking a second slot: a controller that asks again has abandoned the
first, and letting one peer hold several is how a table of six starves at
two controllers -- the same failure this table exists to end.

**called by** `on_subscribe_request`  ·  **calls** `sub_of_session`

### `static void send_report_chunk(struct sub_state *s)`
`ports/dwm3001cdk/app/src/matter_commission.c:1075`

Send one chunk of the priming report.
The whole data model does not fit one Matter message -- the spec caps a
message at the IPv6 MTU and this node's answer measured 1479 bytes of payload
against a ~1232 byte ceiling. An oversized datagram is not slow, it is never
delivered, and the subscriber re-subscribes forever with nothing to say why.

**called by** `on_status_response`, `on_subscribe_request`  ·  **calls** `send_im`

### `static void on_subscribe_request(const struct matter_exchange_in *in)`
`ports/dwm3001cdk/app/src/matter_commission.c:1117`

Begin a subscription.
The order is not the obvious one. A SubscribeRequest is answered with the
REPORT, not with the SubscribeResponse: the subscriber acknowledges that
report with a StatusResponse, and only then is the SubscribeResponse sent
(ReadHandler.cpp:240-250). Answering the request directly leaves the
subscriber holding an id for a subscription whose initial values never
arrived, which is indistinguishable from a node that stopped reporting.

**called by** `on_secure`  ·  **calls** `current_session_id`, `send_report_chunk`, `sub_alloc`

### `static int on_aliro_credential(uint8_t credential_type, const uint8_t public_key[65])`
`ports/dwm3001cdk/app/src/matter_commission.c:1240`

An Aliro credential public key, handed to the reader's trust store -- but only
if it is a key a phone will ever present.
The trust check is a raw-key allowlist (aliro_reader.c), so an anchor is a
claim that some device will present exactly these 65 bytes. An ISSUER key
never will: it identifies the home that certifies credentials, not a device.
Storing it produced a reader that reported "1 trust anchor(s)", looked
provisioned, and rejected every phone one step after "device signature OK" --
measured across three pairings on 2026-08-02, where the stored anchor was
byte-identical every time and the presented key was different every time.
The ESP32 lock, which is the working reference in this repo, has always gated
this on the two endpoint types (door_lock_callbacks.cpp:112-114). This is that
rule, arrived at the long way round.
The issuer key is still ACCEPTED: refusing it would tell the controller this
node cannot hold one, which is a different and equally untrue claim. It is
simply not an anchor. An empty store is the honest report of a reader no
phone can open yet, and it is what makes the next endpoint key visible.

### `static size_t send_sigma2(const struct matter_case_sigma1 *s1, const uint8_t *ipk, const uint8_t *sigma1, size_t sigma1_len, const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`ports/dwm3001cdk/app/src/matter_commission.c:1478`

Build and frame the Sigma2 answering @p s1.
@param sigma1 the Sigma1 payload EXACTLY as it arrived -- the transcript hash
is over those bytes, and rebuilding them would be rebuilding something
the peer hashed and this node did not.

**called by** `matter_thread_on_datagram`

### `static size_t handle_sigma3(const uint8_t *sigma3, size_t sigma3_len, const uint8_t *ipk, const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`ports/dwm3001cdk/app/src/matter_commission.c:1733`

Answer a Sigma3, which ends the handshake.
Sigma2 asked the initiator to believe this node; Sigma3 is the initiator
proving the same thing back, and it is the last message either side sends in
the clear. What follows it is encrypted under keys neither side transmitted,
so a mistake here surfaces as silence on the NEXT message rather than as a
failure on this one -- which is the reason for the checks logged below.

**called by** `matter_thread_on_datagram`  ·  **calls** `case_alloc_slot`, `case_slot_of`, `case_status_report`

### `static size_t case_status_report(const struct matter_proto_header *req, const struct matter_msg_header *req_mh, uint8_t *reply, size_t cap)`
`ports/dwm3001cdk/app/src/matter_commission.c:1906`

The StatusReport that ends CASE.
Still unsecured and still addressed to the initiator's ephemeral id: this is
the last message before the keys take effect, not the first one after.

**called by** `handle_sigma3`

### `size_t matter_thread_on_datagram(const uint8_t *msg, size_t len, uint8_t *reply, size_t cap)`
`ports/dwm3001cdk/app/src/matter_commission.c:1954`

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
`ports/dwm3001cdk/app/src/matter_commission.c:2244`

The link dropped. Cheap here; begin_session() does the real work later.

### `bool matter_commission_has_fabric(void)`
`ports/dwm3001cdk/app/src/matter_commission.c:2261`

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
`ports/dwm3001cdk/app/src/matter_commission.c:2271`

Register the commissioning handlers on the 0xFFF6 transport.
Call after the reader is up. Nothing here touches the radio: whether the
board is discoverable as a commissionable node is decided by the advertising
branch in aliro_ble_zephyr.c, which asks for the Matter payload while this
node holds no fabric -- see matter_commission_has_fabric().
@return 0. A bad verifier is reported by log and refused per attempt rather
than failing startup -- a reader that cannot commission should still
be a reader.

**calls** `load_verifier`

<details><summary>Undocumented (19)</summary>

- `matter_attest_ec_keygen` — tested: matter attest
- `matter_case_sign`
- `matter_case_verify`
- `send_framed`
- `fab_store_work_fn`
- `send_im`
- `on_read_request`
- `on_invoke_request`
- `notify_lock_state`
- `notify_work_fn`
- `notify_lock_state_changed`
- `heartbeat_work_fn`
- `subscription_heartbeat_arm`
- `sub_drop_session`
- `on_aliro_reader_config`
- `on_timed_request`
- `on_status_response`
- `on_secure`
- `on_message`

</details>
