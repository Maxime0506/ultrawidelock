<!-- generated documentation — edit the source, not this file -->
# `scripts/woz_smp.py`

Push a delta patch to the board over SMP, the way a phone would.

WHY THIS EXISTS BESIDE woz_push.py. woz_push speaks the native framed protocol
over an L2CAP CoC, which no phone app can open. This one speaks mcumgr over
GATT -- byte for byte what nRF Device Manager sends -- so the device half can be
proved from a Mac before anyone starts tapping at a phone. When this works and
the app does not, the fault is in the app or the file it is given, not in the
firmware.

    scripts/woz_smp.py build/cdk.woz          push a patch
    scripts/woz_smp.py --list                 read the image list and stop

Requires the board to be built with SMP=1 (firmware/overlay-smp.conf).

CBOR IS HAND-ROLLED HERE, deliberately. The maps mcumgr exchanges are half a
dozen keys of ints and byte strings, and vendoring a CBOR library into the OTA
venv to encode that would be more moving parts than the encoder itself.

## API

### `die(msg)`
`scripts/woz_smp.py:56`

Exit the process with the formatted error message prefixed by woz_smp.

**called by** `Smp.call`, `image_sha`, `run`

### `image_sha(path)`
`scripts/woz_smp.py:61`

The SHA-256 MCUboot recorded in a signed image's TLVs.

The same hash the board reports in its image list, so comparing the two
answers "did the update actually land" with the image's own bytes rather
than with anyone's assumption. Used after a phone push, where nothing else
on this machine witnessed the transfer.

**called by** `run`  ·  **calls** `die`

### `_head(major, n)`
`scripts/woz_smp.py:95`

Encode a CBOR unsigned integer length prefix for the given value n and major type. Returns 1, 2, 3, or 5 bytes depending on the magnitude.

**called by** `cbor_encode`

### `cbor_decode(buf, i=0)`
`scripts/woz_smp.py:129`

Return (value, next_index). Enough of CBOR to read mcumgr's replies.

**called by** `Smp.call`

### `class Smp`
`scripts/woz_smp.py:202`

One mcumgr conversation. Reassembles responses, matches them by seq.

**called by** `run`

#### `Smp.__init__(self, client)`
`scripts/woz_smp.py:205`

Initialize an SMP protocol instance bound to the given BLE client. Tracks the outgoing sequence number, buffers incoming notification chunks, and queues complete frames.

#### `Smp.on_notify(self, _sender, data)`
`scripts/woz_smp.py:212`

Reassemble SMP response frames from BLE notifications. Buffers data and enqueues complete frames once the length declared in the 8-byte header is satisfied.

<details><summary>Undocumented (4)</summary>

- `cbor_encode`
- `Smp.call`
- `run`
- `main`

</details>
