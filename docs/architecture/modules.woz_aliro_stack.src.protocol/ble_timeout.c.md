<!-- generated documentation — edit the source, not this file -->
# `modules/woz_aliro_stack/src/protocol/ble_timeout.c`

@file ble_timeout.c
Aliro BLE timeout supervisor (state machine + reply validator). Core: classify_attribute parses
BLE message type from attribute ID/length; is_allowed_reply maps request→reply types (including
Busy/GeneralError for any); has_response_timeout marks messages that start a timeout window;
collision_replaces_pending resolves priority when incoming messages arrive before the previous
one completes; set_pending / clear_pending manage state transitions. Designed to prevent timeouts
when the phone is responsive and terminate when not.

**depends on** [`modules/woz_aliro_stack/src/protocol/ble_message.h`](ble_message.h.md), [`modules/woz_aliro_stack/src/protocol/ble_timeout.h`](ble_timeout.h.md)

## API

### `static int classify_attribute(const struct woz_aliro_ble_message *message, enum woz_aliro_ble_timeout_message *kind)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:20`

Classify a BLE message by attribute ID and length: returns WOZ_ALIRO_BLE_MALFORMED (bad format),
WOZ_ALIRO_BLE_OK (valid, kind set). Recognizes Busy (id=0, len=0), GeneralError (id=1, len=1),
and ranging-specific types (InitiateRanging, Resume, SetupLater, ResumeLater,
SecureRangingFailed, RangingSuspended).

**called by** `woz_aliro_ble_timeout_classify`

### `int woz_aliro_ble_timeout_classify(const uint8_t *data, size_t data_length, enum woz_aliro_ble_timeout_message *kind)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:69`

Classify one complete, unencrypted Aliro BLE message.

**calls** `classify_attribute`

### `static int has_response_timeout(enum woz_aliro_ble_timeout_message message)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:141`

Test whether a message expects a response within a timeout window (1/0). Applies to
access/ranging/handshake/control requests.

**called by** `woz_aliro_ble_timeout_observe`

### `static int is_allowed_reply(enum woz_aliro_ble_timeout_message request, enum woz_aliro_ble_timeout_message reply)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:161`

Test whether reply is a valid response to request: allows Busy/GeneralError for any request, then
checks request-specific reply rules (e.g., InitiateAccess → ApRequest, ApRequest → ApResponse,
InitiateRanging → SetupM1/SetupLater, etc.). Returns 1 (allowed), 0 (not allowed).

**called by** `woz_aliro_ble_timeout_observe`

### `static int collision_replaces_pending(enum woz_aliro_ble_timeout_message pending, enum woz_aliro_ble_timeout_message incoming)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:205`

Test whether an incoming message should replace a pending one: ResumRequest is replaced by
InitiateRanging/InitiateRangingResume/SuspendRequest; SuspendRequest is replaced by the same
three. Returns 1 (collision), 0 (no collision).

**called by** `woz_aliro_ble_timeout_observe`

### `static void set_pending(struct woz_aliro_ble_timeout_state *state, enum woz_aliro_ble_timeout_direction direction, enum woz_aliro_ble_timeout_message message)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:225`

Set pending message and role (local transmitter if outgoing, local receiver if incoming) in the
BLE timeout state machine.

**called by** `woz_aliro_ble_timeout_observe`

### `static void clear_pending(struct woz_aliro_ble_timeout_state *state)`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:238`

Clear the pending message and timeout role in the BLE timeout state machine.

**called by** `woz_aliro_ble_timeout_observe`

### `woz_aliro_ble_timeout_observe`
`modules/woz_aliro_stack/src/protocol/ble_timeout.c:251`

Supervise Aliro BLE protocol timeouts in a state machine: track pending messages, role (idle,
local transmitter, local receiver), and deadline. Given a message direction and type, return the
action (ARM timeout, STOP timeout, TERMINATE connection, or HOLD). Validates collisions (incoming
ResumRequest or SuspendRequest can replace pending messages) and enforces request-reply
correspondence.

**calls** `clear_pending`, `collision_replaces_pending`, `has_response_timeout`, `is_allowed_reply`, `set_pending`
