#!/usr/bin/env python3
"""Push a signed delta patch to a DWM3001CDK over Bluetooth.

    scripts/woz_push.py update.wdfu

The board accepts nothing until an update window is open. Press SW2 first; the
window lasts CONFIG_WOZ_DFU_WINDOW_MS (five minutes by default). If you see
"no update window open", that is what happened.

On success the board reboots into MCUboot, which applies the patch -- about
30 seconds during which it is not on the air. The Bluetooth connection dropping
right after COMMIT is the expected ending, not a failure.

Needs bleak:

    python3 -m pip install bleak

WHY GATT AND NOT THE L2CAP CoC the firmware also offers: no Python Bluetooth
library can open an L2CAP connection-oriented channel. CoreBluetooth and BlueZ
both can, bleak wraps neither, and bleak is the only cross-platform option. The
firmware carries both transports for exactly this reason; an iPhone app would
use the CoC and get better throughput.
"""

import argparse
import asyncio
import struct
import sys
from pathlib import Path

DFU_SVC_UUID = "d3b5a140-9e23-4b3a-8be4-6b1ee5f980a3"
DFU_CHR_UUID = "d3b5a141-9e23-4b3a-8be4-6b1ee5f980a3"

OP_BEGIN, OP_DATA, OP_COMMIT, OP_ABORT = 0x01, 0x02, 0x03, 0x04
RSP_OK, RSP_ERR = 0x81, 0x82

ERRORS = {
    1: "no update window is open -- press SW2 on the board, then retry",
    2: "out of sequence",
    3: "too large for patch_staging",
    4: "header signature did not verify (wrong signing key?)",
    5: "length or CRC disagreed at commit",
    6: "a flash write or erase failed",
    7: "malformed frame",
}


def die(msg):
    sys.exit(f"woz_push: {msg}")


class Session:
    """One update conversation: write a frame, wait for its reply."""

    def __init__(self, client):
        self.client = client
        self.replies = asyncio.Queue()

    def on_notify(self, _sender, data):
        self.replies.put_nowait(bytes(data))

    async def call(self, frame, timeout=20.0):
        await self.client.write_gatt_char(DFU_CHR_UUID, frame, response=True)
        try:
            rsp = await asyncio.wait_for(self.replies.get(), timeout)
        except asyncio.TimeoutError:
            die("the board stopped answering")

        if not rsp:
            die("empty reply")
        if rsp[0] == RSP_ERR:
            code = rsp[1] if len(rsp) > 1 else 0
            die(f"refused: {ERRORS.get(code, f'unknown error {code}')}")
        if rsp[0] != RSP_OK:
            die(f"unexpected reply {rsp[0]:#04x}")
        return struct.unpack("<I", rsp[1:5])[0] if len(rsp) >= 5 else 0


async def run(args):
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError:
        die("needs the 'bleak' module: python3 -m pip install bleak")

    blob = Path(args.patch).read_bytes()
    if len(blob) < 96:
        die(f"{args.patch} is too short to be a .wdfu")

    # The DFU service is NOT in the advertisement -- the advertising set is the
    # reader's and is already full -- so match on what the board does advertise:
    # Aliro 0xFFF2 once provisioned, Matter 0xFFF6 while commissionable.
    wanted = {
        "0000fff2-0000-1000-8000-00805f9b34fb",
        "0000fff6-0000-1000-8000-00805f9b34fb",
    }

    print("  scanning ...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or "") == args.name
        or bool(wanted & {u.lower() for u in (ad.service_uuids or [])}),
        timeout=args.scan_timeout,
    )
    if device is None:
        die("no board found advertising Aliro (0xFFF2) or Matter (0xFFF6). "
            "Is it powered and advertising?")
    print(f"  found {device.address}")

    # services=[...] is not an optimisation. Without it CoreBluetooth walks every
    # characteristic on the board and fails on one of the reader's with
    # "The specified UUID is not allowed for this operation" (CBError 8), which
    # aborts the connection before the DFU service is ever reached.
    async with BleakClient(device, services=[DFU_SVC_UUID]) as client:
        session = Session(client)
        await client.start_notify(DFU_CHR_UUID, session.on_notify)

        # One opcode byte plus ATT's three, out of whatever MTU was negotiated.
        mtu = getattr(client, "mtu_size", 23) or 23
        chunk = max(16, min(args.chunk, mtu - 4))
        print(f"  connected, MTU {mtu}, sending {len(blob):,} B in {chunk} B chunks")

        await session.call(struct.pack("<BI", OP_BEGIN, len(blob)))

        sent = 0
        while sent < len(blob):
            piece = blob[sent:sent + chunk]
            got = await session.call(bytes([OP_DATA]) + piece)
            sent += len(piece)
            if got != sent:
                die(f"board counted {got} B after {sent} B were sent")
            pct = 100.0 * sent / len(blob)
            print(f"\r  {sent:>7,} / {len(blob):,} B  {pct:5.1f}%", end="", flush=True)
        print()

        await session.call(bytes([OP_COMMIT]))
        print("  committed. The board is rebooting to apply it (~30 s).")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("patch", help="a .wdfu built by scripts/woz_patch.py")
    p.add_argument("--name", default="openaliro", help="advertised name to look for")
    p.add_argument("--chunk", type=int, default=180,
                   help="bytes of patch per frame, capped by the negotiated MTU")
    p.add_argument("--scan-timeout", type=float, default=15.0)
    asyncio.run(run(p.parse_args()))


if __name__ == "__main__":
    main()
