#!/usr/bin/env python3
# Copyright (c) 2026 asxeem
# SPDX-License-Identifier: ISC
"""Inspect an aliro_prov ("APRV") reader-provisioning blob.

The blob is the unit of the clone path: a board commissioned into Apple Home
exports one with `aliro export`, and a board that cannot be commissioned adopts
it. This tool answers the question that otherwise costs a hardware cycle to ask
-- is this blob actually carrying an Apple-issued credential, or is it the dev
identity with nothing in it?

Two inputs, auto-detected:

  aliro_blob.py 41505256030...      a hex string, as printed by `aliro export`
  aliro_blob.py nvs.bin             a file, scanned for every APRV blob in it

The file form works on a raw `esptool.py read_flash` dump of the ESP32 nvs
partition, which is a read-only way to recover the credential from a board you
do not want to reflash.

Wire format is modules/woz_aliro/src/aliro_prov.c (serialize at :64,
deserialize at :123); the checks below mirror what the firmware enforces.
"""

import argparse
import hashlib
import re
import sys

MAGIC = b"APRV"
HDR = 6  # magic(4) + version(1) + flags(1)
READER_ID_LEN = 32
PRIV_LEN = 32
GRK_LEN = 16
CRED_PUB_LEN = 65
KPERSISTENT_LEN = 32
TRUST_MAX = 4
FLAG_DEV = 0x01

# modules/woz_aliro/src/aliro_prov.c:25-34. Present in every image, so a blob
# carrying these is a board that was never provisioned by Apple, whatever its
# is_dev flag says.
DEV_READER_ID = bytes.fromhex(
    "113b1a9ef29567608b7500fbaca609e9c07b874e182ae565024b543e3b40935f"
)
DEV_SIGN_PRIV = bytes.fromhex(
    "4d332169f4339eef549ef2a6a94b6195a42fc2af8ccfdfce35bdf9bed8f383a3"
)


class BadBlob(Exception):
    pass


def parse(buf, off=0):
    """Parse one blob at buf[off:]. Returns (fields, total_len)."""
    if len(buf) - off < HDR or buf[off : off + 4] != MAGIC:
        raise BadBlob("no APRV magic")

    version = buf[off + 4]
    if version == 3:
        grk_len, has_kp = GRK_LEN, True
    elif version == 2:
        grk_len, has_kp = GRK_LEN, False
    elif version == 1:
        grk_len, has_kp = 0, False
    else:
        raise BadBlob(f"unknown version {version}")

    fixed = HDR + READER_ID_LEN + PRIV_LEN + grk_len + 1
    if len(buf) - off < fixed:
        raise BadBlob("truncated before the credential count")

    count = buf[off + fixed - 1]
    total = fixed + count * CRED_PUB_LEN
    if has_kp:
        total += 1 + count * KPERSISTENT_LEN
    if count > TRUST_MAX:
        raise BadBlob(f"credential count {count} over the {TRUST_MAX} the reader stores")
    if len(buf) - off < total:
        raise BadBlob("truncated in the credential list")

    p = off + HDR
    reader_id = buf[p : p + READER_ID_LEN]
    sign_priv = buf[p + READER_ID_LEN : p + READER_ID_LEN + PRIV_LEN]
    grk = (
        buf[p + READER_ID_LEN + PRIV_LEN : p + READER_ID_LEN + PRIV_LEN + GRK_LEN]
        if grk_len
        else bytes(GRK_LEN)
    )

    k = off + fixed
    creds = []
    for _ in range(count):
        creds.append(buf[k : k + CRED_PUB_LEN])
        k += CRED_PUB_LEN
    kp_valid = 0
    if has_kp:
        kp_valid = buf[k] & ((1 << count) - 1)

    return (
        {
            "version": version,
            "is_dev": bool(buf[off + 5] & FLAG_DEV),
            "reader_id": reader_id,
            "sign_priv": sign_priv,
            "grk": grk,
            "creds": creds,
            "kp_valid": kp_valid,
        },
        total,
    )


def check(f):
    """Return the list of reasons this blob will not produce a Wallet unlock."""
    fatal = []
    if f["is_dev"] or f["reader_id"] == DEV_READER_ID or f["sign_priv"] == DEV_SIGN_PRIV:
        fatal.append(
            "this is the built-in DEV identity, not an Apple-issued one. The source "
            "board was never provisioned, or was factory-reset after it was."
        )
    if not any(f["grk"]):
        fatal.append(
            "GroupResolvingKey is all zero, so the reader cannot advertise a "
            "resolvable dynamic tag and Wallet will never approach it. Apple sets "
            "the GRK in SetAliroReaderConfig; a zero one means that command never "
            "landed."
        )
    if not f["creds"]:
        fatal.append(
            "no trust anchors, so no phone key is enrolled and every transaction "
            "will be rejected at the credential check."
        )
    return fatal


def report(f, total, args, where=""):
    priv_id = hashlib.sha256(f["sign_priv"]).hexdigest()[:16]
    print(f"APRV v{f['version']}, {total} bytes{where}")
    print(f"  identity          {'DEV (built-in)' if f['is_dev'] else 'provisioned'}")
    print(f"  groupIdentifier   {f['reader_id'][:16].hex()}")
    print(f"  groupSubIdent.    {f['reader_id'][16:].hex()}")
    print(
        "  signing key       "
        + (f["sign_priv"].hex() if args.show_private else f"sha256:{priv_id}... (private; --show-private to reveal)")
    )
    print(
        f"  GRK               {f['grk'].hex()}"
        + ("  <- all zero" if not any(f["grk"]) else "")
    )
    print(f"  trust anchors     {len(f['creds'])} of {TRUST_MAX}")
    for i, c in enumerate(f["creds"]):
        kp = "yes" if f["kp_valid"] & (1 << i) else "no"
        print(f"    [{i}] {c[:8].hex()}...{c[-4:].hex()}  Kpersistent: {kp}")

    fatal = check(f)
    if fatal:
        print("  VERDICT: will not unlock")
        for r in fatal:
            print(f"    - {r}")
    else:
        print("  VERDICT: usable. Provisioned identity, live GRK, at least one phone key.")
    return not fatal


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="hex string from `aliro export`, or a path to a flash/NVS dump")
    ap.add_argument("--show-private", action="store_true", help="print the reader private key")
    ap.add_argument("--import-cmd", "--kconfig", dest="import_cmd", action="store_true",
                    help="emit the `aliro import` line to paste into the provisioning console")
    args = ap.parse_args()

    src = args.source.strip()
    if re.fullmatch(r"[0-9a-fA-F]+", src) and len(src) % 2 == 0 and len(src) >= 2 * HDR:
        blobs = [(bytes.fromhex(src), 0, "")]
    else:
        try:
            data = open(src, "rb").read()
        except OSError as e:
            sys.exit(f"not a hex string and not a readable file: {e}")
        hits = [m.start() for m in re.finditer(re.escape(MAGIC), data)]
        if not hits:
            sys.exit(f"no APRV blob found in {src} ({len(data)} bytes scanned)")
        blobs, seen = [], set()
        for off in hits:
            try:
                _, total = parse(data, off)
            except BadBlob:
                continue  # a stray "APRV" in unrelated bytes
            raw = data[off : off + total]
            if raw not in seen:
                seen.add(raw)
                blobs.append((raw, 0, f"  at offset 0x{off:x} of {src}"))
        if not blobs:
            sys.exit(f"found the APRV magic in {src} but no blob parsed cleanly")
        if len(blobs) > 1:
            print(
                f"note: {len(blobs)} distinct blobs in this dump. NVS keeps superseded "
                "copies, so prefer the one that passes the checks below.\n"
            )

    ok = False
    for raw, off, where in blobs:
        try:
            f, total = parse(raw, off)
        except BadBlob as e:
            print(f"malformed{where}: {e}")
            continue
        if report(f, total, args, where):
            ok = True
            if args.import_cmd:
                print(f"\naliro import {raw[:total].hex()}")
        print()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
