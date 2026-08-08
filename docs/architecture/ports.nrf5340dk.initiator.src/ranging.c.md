<!-- generated documentation — edit the source, not this file -->
# `ports/nrf5340dk/initiator/src/ranging.c`

Device-side UWB ranging-setup driver: opens the reader's BleSK-sealed SDUs and
answers them, walking AP-Completed -> Initiate-Ranging-Session -> M1 -> M2 ->
M3 -> M4 until the reader starts its responder.

**depends on** [`ports/nrf5340dk/initiator/src/prepoll_tx.h`](prepoll_tx.h.md), [`ports/nrf5340dk/initiator/src/ranging.h`](ranging.h.md)

## API

### `static int send_plain(const uint8_t *plain, size_t plain_len)`
`ports/nrf5340dk/initiator/src/ranging.c:79`

Seal one plaintext ranging SDU on the BleSK channel and put it on the wire.
There is no outer envelope. A sealed SDU is already [proto][id][len_be16][ct||tag],
the same four-byte shape aliro_ble_frame produces, so framing it again would
give the reader a header describing a header.

**called by** `send_initiate_ranging`, `send_message`

### `static int send_message(struct aliro_uwb_message *msg, const char *what)`
`ports/nrf5340dk/initiator/src/ranging.c:99`

Send a built M2/M4 and free it, whatever the outcome.

**called by** `on_m1`, `on_m3`  ·  **calls** `send_plain`

### `static int send_initiate_ranging(void)`
`ports/nrf5340dk/initiator/src/ranging.c:124`

Ask the reader to begin ranging setup.
Notification / Ranging / Initiate-Ranging, a zero-length attribute. The reader
routes this to aliro_uwb_session_init_setup(), which is the ONLY thing that
makes it emit M1 (aliro_uwb_msg.c, parse_ranging_notification -> the
INIT_RANGING case, and aliro_uwb_session.c's init_setup). Nothing else starts
the exchange, exactly as Initiate-Access-Protocol is what starts AUTH0.

**called by** `initiator_ranging_on_sdu`  ·  **calls** `send_plain`

### `static void on_m1(const uint8_t *plain, size_t plain_len)`
`ports/nrf5340dk/initiator/src/ranging.c:144`

Answer M1 with M2: parse what the reader offered, pick from it, send.

**called by** `initiator_ranging_on_sdu`  ·  **calls** `send_message`

### `static void on_m3(const uint8_t *plain, size_t plain_len)`
`ports/nrf5340dk/initiator/src/ranging.c:195`

Answer M3 with M4.
M4 carries the values the DEVICE picks for the round: the initial STS index,
the UWB clock reference, the hop-mode key and one sync code.
Three of the four are now load-bearing. STS_Index0 feeds the UAD derivation
that produces the Pre-POLL's SrcLongAddr, so it is an input to the MIC both
sides compute; the sync code is the preamble code the frame is sent on; the
hop-mode key keys the round schedule. They stay literals only because the
device is free to choose them -- any value works so long as both ends use the
same one, which M4 is what guarantees.
uwb_time0 is the exception and is still a meaningless 0. It is a DW3000
timestamp the reader would schedule its first RX slot against, and this board
is not yet keeping a block clock to give it one. That costs nothing today,
because the reader's Pre-POLL listener is a continuous self-rearming RX
rather than a scheduled window. It starts to matter at the POLL.
These are also the constants tests/host/test_aliro_device_uwb.c:124-134
drives the real reader session with, where they take it to state RANGING.

**called by** `initiator_ranging_on_sdu`  ·  **calls** `send_message`

### `void initiator_ranging_begin(struct aliro_device *dev, uint16_t conn)`
`ports/nrf5340dk/initiator/src/ranging.c:246`

Arm the ranging-setup driver for @conn, once the Access Protocol has reached
ESTABLISHED and @dev->sc_ble is therefore keyed. Idempotent per connection.

### `void initiator_ranging_end(uint16_t conn)`
`ports/nrf5340dk/initiator/src/ranging.c:255`

Drop the ranging state for @conn. Safe to call for a connection that never
reached ESTABLISHED.

### `int initiator_ranging_on_sdu(uint16_t conn, const uint8_t *wire, size_t wire_len)`
`ports/nrf5340dk/initiator/src/ranging.c:268`

Feed one inbound post-auth SDU, still sealed, exactly as it came off L2CAP
(wire = [proto][id][len_be16][ct||tag]). Opens it on the BleSK channel and
answers if the message calls for an answer. Returns 0 if the SDU was consumed,
<0 if it was not ours or could not be opened.

**calls** `on_m1`, `on_m3`, `send_initiate_ranging`, `step_str`

<details><summary>Undocumented (1)</summary>

- `step_str`

</details>
