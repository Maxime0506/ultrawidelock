# Aliro initiator (ESP32-S3)

The User-Device half of Aliro, the role a phone plays. This app exists so the
reader can be exercised on the bench with no iPhone in the loop.

**Stage 1a: BLE transport plus the Access Protocol.** It discovers the reader,
opens the channel, runs the credential-auth handshake to completion, and prints
the URSK both sides agreed. It does not range: that is Stage 1b and needs a
DWM3000 on this board.

## What it does

1. Scans for the reader's `0xFFF2` service-data advert
2. Connects, discovers the Aliro GATT service
3. **Reads** the reader-SPSM characteristic to get the L2CAP PSM, the reader's
   supported protocol versions, and its feature byte
4. **Writes** the version this device selects
5. Opens an L2CAP CoC to the PSM from step 3
6. Builds the BleSK salt from the version list step 3 returned, and arms the
   device state machine with it
7. Answers each `AUTH0` / `AUTH1` / `EXCHANGE` command the reader sends, ending
   in a shared 32-byte URSK

Step 3 is not optional, for two separate reasons. The PSM (`0x0080`) sits in the
dynamic range, so there is no well-known value to connect to. And the version
list it returns *is* the BleSK salt input, which differs per reader: the ESP32
reader publishes v1.0 alone, the nRF publishes two entries. A compiled-in default
would derive a key the nRF does not share.

## Credentials

The reader identity in `main/main.c` is the **dev identity** every unprovisioned
reader falls back to (`aliro_prov_dev_default`). `k_reader_id` is that constant
verbatim and `k_reader_verif_pub` is the public point of its signing key. As a
free consistency check, `k_reader_id` is exactly that point's X coordinate.

This works only against a reader that is **still on its dev identity and has an
empty trust store**. That combination makes the reader accept any presented
credential, so the initiator's own throwaway keypair is enough. On the nRF that
means building with our stack rather than the prebuilt one:

```sh
make build ALIRO_SOURCE=1 NFC=none
```

`NFC=none` is not required, but the initiator never taps, and it removes the NFC
frontend as a way for the bench to fail.

A reader provisioned over Matter has a real identity and enforces its trust
store. Against one of those these constants are wrong and the transaction fails
at AUTH1. That is the expected outcome, not a bug, and it is the reason the app
still latches onto any reader it sees rather than filtering by identity: a
filtered scan would fail silently instead.

The credential keypair is a test fixture in a public repository. Never provision
it into anything that guards a door.

## Build and flash

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3      # fresh tree only
idf.py build
idf.py -p <PORT> flash monitor
```

Expect, once a reader is powered nearby:

```
I (…) aliro_central: found our reader (rssi -42); connecting
I (…) aliro_central: connected (conn 0); discovering 0xFFF2
I (…) aliro_central: peer SPSM 0x0080, 1 version(s), features 0x…
I (…) aliro_central: coc connected (conn 0, SPSM 0x0080)
I (…) initiator: === transport up (conn 0) ===
I (…) initiator: device armed; waiting for the reader to send AUTH0
I (…) initiator: conn 0: -> 74 B response, phase SENT_AUTH0_RESP (send rc=0)
I (…) initiator: conn 0: -> 141 B response, phase SENT_AUTH1_RESP (send rc=0)
I (…) initiator: === ESTABLISHED: URSK agreed with the reader ===
```

Response sizes are illustrative. The phases are not.

## What comes next

Stage 1b is the UWB initiator round on the agreed URSK, which needs a DWM3000
fitted here. Separately, running against a **stock** nRF (Nordic's prebuilt Aliro
stack, provisioned over Matter) is the more valuable test: it is the nearest
thing to a third-party Aliro reader on the bench, and the only non-self-
referential check short of a real iPhone.

## Known limits

- One envelope per SDU on receive. Our reader sends exactly that, but a reader
  that coalesces would have its trailing envelopes dropped here.
- The fast path is not implemented. `AUTH0Response` always carries a NULL
  cryptogram, so every transaction takes the standard path.
- A real iPhone sends a proto-3 `SUPPLEMENTARY_SERVICE` message just before
  Initiate-Ranging. This app does not, which is a tell a foreign lock could use.
