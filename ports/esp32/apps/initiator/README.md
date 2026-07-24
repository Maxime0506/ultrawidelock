# Aliro initiator (ESP32-S3)

The User-Device half of Aliro — the role a phone plays. This app exists so the
reader can be exercised on the bench with no iPhone in the loop.

**Stage 1a: BLE transport only.** It discovers the reader and opens the channel,
then stops. It does not run the Aliro transaction yet.

## What it does

1. Scans for the reader's `0xFFF2` service-data advert
2. Connects, discovers the Aliro GATT service
3. **Reads** the reader-SPSM characteristic → the L2CAP PSM, the reader's
   supported protocol versions, and its feature byte
4. **Writes** the version this device selects
5. Opens an L2CAP CoC to the PSM from step 3 and logs what it learned

Step 3 is not optional. The PSM (`0x0080`) sits in the dynamic range, so there is
no well-known value to connect to — the reader publishes it and the initiator has
to read it first.

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
```

## Pointing it at a specific reader

`k_reader_id` in `main/main.c` is all zeros out of the box, which means *not
provisioned*: the app latches onto the first Aliro reader it sees and logs that
reader's group id. That is a bench affordance, not a mode to ship — with two
readers in range it will pick whichever advertises first.

The full 32-byte reader identity cannot be recovered from the air. The advert
carries only `reader_id[0..7]` and `reader_id[16..17]`, so the real value has to
be copied from the reader once, out of band, exactly as a phone is provisioned.

## What comes next

Running the transaction over this channel needs the initiator to hold an Access
Credential the reader trusts. The device-side protocol stack for it is already
written and host-tested (`modules/woz_aliro/aliro_device.c`); what is missing is
provisioning both ends with a matching identity. After that comes Stage 1b, the
UWB initiator round, which needs a DWM3000 on this board.
