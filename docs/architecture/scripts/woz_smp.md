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

### `image_sha(path)`
`scripts/woz_smp.py:60`

The SHA-256 MCUboot recorded in a signed image's TLVs.

The same hash the board reports in its image list, so comparing the two
answers "did the update actually land" with the image's own bytes rather
than with anyone's assumption. Used after a phone push, where nothing else
on this machine witnessed the transfer.

**called by** `run`  ·  **calls** `die`

### `cbor_decode(buf, i=0)`
`scripts/woz_smp.py:127`

Return (value, next_index). Enough of CBOR to read mcumgr's replies.

**called by** `Smp.call`

### `class Smp`
`scripts/woz_smp.py:200`

One mcumgr conversation. Reassembles responses, matches them by seq.

**called by** `run`

<details><summary>Undocumented (8)</summary>

- `die`
- `_head`
- `cbor_encode`
- `Smp.__init__`
- `Smp.on_notify`
- `Smp.call`
- `run`
- `main`

</details>
