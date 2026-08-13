#!/usr/bin/env python3
"""mcuboot-keyhash-check.py — will THIS bootloader boot THAT image?

    scripts/mcuboot-keyhash-check.py --bootloader boot.bin --image app.hex
    scripts/mcuboot-keyhash-check.py --bootloader boot.bin        # just print it
    scripts/mcuboot-keyhash-check.py --self-test

Exit 0 they match, 1 they do not, 2 the check could not do its job.

WHY THIS EXISTS, and why check-signing-key.sh does not already cover it.

That script answers a different question, and a real one: it refuses a build
that signs with MCUboot's own published demo key, which every stock bootloader
in the world accepts. It says nothing about whether the key you are signing
with is the key a PARTICULAR BOARD trusts.

Nothing did, and the gap cost a session twice. Once tracking down why a
correctly built image would not boot, which turned out to be four different
keys across four worktrees and no way to tell which was live. Then again, worse,
when the surviving copy of the key a board's bootloader trusted was destroyed by
pruning the worktree that held it -- discovered only after signing three
candidates and watching each one fail. The keys are gitignored, so no clone,
worktree or push carries them; they live in working directories and nowhere
else, and `make dfu-key` regenerates one right up until a bootloader is flashed,
after which a fresh key is worthless for that board.

Both failures are the same missing fact: the bootloader already knows which key
it trusts, and it is sitting in the binary. MCUboot compiles the public half in
as a DER SubjectPublicKeyInfo, and the image's KEYHASH TLV is a SHA-256 of
exactly those bytes. So the comparison needs no key material, no private half
and no build system -- two files and a hash.

That is also why this is safe to run anywhere and to put in a log: everything it
touches is public. It never reads a private key and cannot.
"""
import argparse
import hashlib
import struct
import sys

# DER SubjectPublicKeyInfo prefix for an EC P-256 public key: AlgorithmIdentifier
# id-ecPublicKey + prime256v1, then a 66-byte BIT STRING holding the 0x04
# uncompressed point marker and two 32-byte coordinates. 26 + 65 = 91 bytes.
P256_SPKI_PREFIX = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
)
P256_SPKI_LEN = 91

IMAGE_MAGIC = 0x96F3B83D
TLV_INFO_MAGIC = 0x6907
TLV_PROT_INFO_MAGIC = 0x6908
TLV_KEYHASH = 0x01


def read_any(path):
    """Bytes from a .hex or a raw .bin, addressed from zero either way.

    Intel HEX is parsed here rather than with intelhex, so this runs on a bare
    python3 -- a gate that needs a virtualenv is one people skip.
    """
    raw = open(path, "rb").read()
    if not raw.lstrip()[:1] == b":":
        return raw

    out = bytearray()
    base = 0
    for line in raw.decode("ascii", "replace").splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        count, addr, rectype = b[0], struct.unpack(">H", b[1:3])[0], b[3]
        data = b[4 : 4 + count]
        if rectype == 0x00:
            at = base + addr
            if at + count > len(out):
                out.extend(b"\xff" * (at + count - len(out)))
            out[at : at + count] = data
        elif rectype == 0x04:
            base = struct.unpack(">H", data)[0] << 16
        elif rectype == 0x02:
            base = struct.unpack(">H", data)[0] << 4
    return bytes(out)


def bootloader_keyhashes(blob):
    """Every P-256 public key compiled into the bootloader, as its TLV hash."""
    out = []
    i = blob.find(P256_SPKI_PREFIX)
    while i != -1:
        spki = blob[i : i + P256_SPKI_LEN]
        if len(spki) == P256_SPKI_LEN:
            out.append(hashlib.sha256(spki).hexdigest())
        i = blob.find(P256_SPKI_PREFIX, i + 1)
    return out


def image_keyhash(blob):
    """The KEYHASH TLV of a signed image, wherever in the blob it starts.

    A .hex straight from imgtool is addressed at the slot's real offset, so the
    header is not at zero. Rather than take the slot base as an argument -- one
    more thing to get wrong and no way to notice -- find the magic.
    """
    start = 0
    while True:
        idx = blob.find(struct.pack("<I", IMAGE_MAGIC), start)
        if idx == -1:
            return None, "no MCUboot image header found"
        hdr = blob[idx : idx + 32]
        if len(hdr) < 16:
            return None, "image header truncated"
        _, _, hdr_size, _, img_size, _ = struct.unpack("<IIHHII", hdr[:20][:16] + hdr[16:20])
        off = idx + hdr_size + img_size
        if off + 4 <= len(blob):
            magic, total = struct.unpack("<HH", blob[off : off + 4])
            if magic in (TLV_INFO_MAGIC, TLV_PROT_INFO_MAGIC):
                # A protected block comes first when present; the keyhash lives
                # in the unprotected one after it.
                if magic == TLV_PROT_INFO_MAGIC:
                    off += total
                    if off + 4 > len(blob):
                        return None, "protected TLV block runs past the image"
                    magic, total = struct.unpack("<HH", blob[off : off + 4])
                    if magic != TLV_INFO_MAGIC:
                        return None, "no unprotected TLV block after the protected one"
                p = off + 4
                end = off + total
                while p + 4 <= min(end, len(blob)):
                    t, ln = struct.unpack("<HH", blob[p : p + 4])
                    if t == TLV_KEYHASH:
                        return blob[p + 4 : p + 4 + ln].hex(), None
                    p += 4 + ln
                return None, "image is signed but carries no KEYHASH TLV"
        start = idx + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bootloader")
    ap.add_argument("--image")
    ap.add_argument(
        "--boot-size",
        default=None,
        help="bytes of --bootloader that are the bootloader (e.g. 0xa000). "
        "REQUIRED when passing a full-flash dump: the application region "
        "holds P-256 keys of its own -- reader trust anchors -- and a scan "
        "that reaches them would report keys this bootloader does not trust.",
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.bootloader:
        sys.stderr.write("need --bootloader (and usually --image)\n")
        return 2

    try:
        boot = read_any(args.bootloader)
    except OSError as e:
        sys.stderr.write(f"keyhash-check: cannot read bootloader: {e}\n")
        return 2

    if args.boot_size is not None:
        boot = boot[: int(args.boot_size, 0)]

    trusted = bootloader_keyhashes(boot)
    if not trusted:
        sys.stderr.write(
            "keyhash-check: no P-256 public key found in the bootloader.\n"
            "  Either this is not an MCUboot binary, or it was built for a\n"
            "  different signature type. Refusing to guess.\n"
        )
        return 2

    if len(trusted) > 1 and args.boot_size is None:
        # Almost always a full-flash dump rather than a multi-key bootloader.
        # Refusing beats reporting, because the extra entries would widen what
        # this gate accepts -- an image signed by a reader trust anchor would
        # read as trusted -- which is the opposite of the job.
        sys.stderr.write(
            f"keyhash-check: found {len(trusted)} P-256 keys in "
            f"{args.bootloader}.\n"
            "  A bootloader normally compiles in one. This is what a\n"
            "  full-flash dump looks like: the application's own keys are\n"
            "  being counted as bootloader keys, which would widen what this\n"
            "  check accepts rather than narrow it.\n"
            "  Pass --boot-size (0xa000 on this board) to bound the scan.\n"
        )
        return 2

    for h in trusted:
        print(f"  bootloader trusts {h}")

    if not args.image:
        return 0

    try:
        img = read_any(args.image)
    except OSError as e:
        sys.stderr.write(f"keyhash-check: cannot read image: {e}\n")
        return 2

    got, err = image_keyhash(img)
    if err:
        sys.stderr.write(f"keyhash-check: {err}\n")
        return 2
    print(f"  image signed by  {got}")

    if got in trusted:
        print("  match: this bootloader will accept this image")
        return 0

    sys.stderr.write(
        "\nkeyhash-check: THIS BOOTLOADER WILL REJECT THIS IMAGE.\n"
        "  The signing key does not match the one compiled into the\n"
        "  bootloader on this board. The keys are gitignored, so they exist\n"
        "  only in working directories -- check other worktrees before\n"
        "  assuming the right one still exists.\n"
    )
    return 1


def self_test():
    """Prove the refusal fires, because a gate that only ever passes is not one."""
    import os
    import tempfile

    ok = True

    # THE CASE THIS SELF-TEST COULD NOT SEE.
    #
    # Every other check below builds its fixture out of P256_SPKI_PREFIX and
    # then searches for P256_SPKI_PREFIX. If that constant were wrong -- a bad
    # transcription, or MCUboot changing how it embeds the key -- the fixture
    # would be wrong in exactly the same way and all four would still pass. The
    # check would be scanning real bootloaders and finding nothing, while its
    # own self-test reported PASS.
    #
    # The real path does fail closed on an empty scan ("no P-256 public key
    # found ... Refusing to guess"), so the blindness points the safe way. But
    # "safe" and "verified" are different claims and this only ever made the
    # first one.
    #
    # So derive the prefix a second way, from a real generated P-256 key rather
    # than from the literal. cryptography is not a dependency of this script --
    # it has to run on bare python3 -- so when it is absent this says so instead
    # of quietly skipping.
    #
    # DO NOT SIMPLIFY THIS TO USE P256_SPKI_PREFIX. Generating a key to check a
    # constant that is right there in the file looks like waste, and replacing
    # it with a comparison against the constant would pass, shorten the file,
    # and read as a tidy-up. It would also restore the exact bug this exists to
    # close: the value under test cannot supply its own evidence. The cost of
    # the second derivation IS the point of it.
    try:
        from cryptography.hazmat.primitives import serialization as _ser
        from cryptography.hazmat.primitives.asymmetric import ec as _ec

        _der = (
            _ec.generate_private_key(_ec.SECP256R1())
            .public_key()
            .public_bytes(_ser.Encoding.DER, _ser.PublicFormat.SubjectPublicKeyInfo)
        )
        if len(_der) != P256_SPKI_LEN:
            print(f"  FAIL a real P-256 SPKI is {len(_der)} bytes, not {P256_SPKI_LEN}")
            ok = False
        elif not _der.startswith(P256_SPKI_PREFIX):
            print(f"  FAIL P256_SPKI_PREFIX does not match a real key: {_der[:27].hex()}")
            ok = False
        else:
            print("  ok   the search prefix matches a genuinely generated P-256 key")
    except ImportError:
        print("  --   search prefix NOT checked against a real key (no cryptography);")
        print("       every case below builds its fixture from the same constant it")
        print("       searches for, so none of them can see a wrong prefix.")

    def build_image(keyhash_bytes):
        body = b"\x00" * 64
        hdr_size, img_size = 32, len(body)
        hdr = struct.pack(
            "<IIHHII", IMAGE_MAGIC, 0, hdr_size, 0, img_size, 0
        ) + b"\x00" * 12
        tlv = struct.pack("<HH", TLV_KEYHASH, len(keyhash_bytes)) + keyhash_bytes
        total = 4 + len(tlv)
        return hdr + body + struct.pack("<HH", TLV_INFO_MAGIC, total) + tlv

    key_a = bytes(range(32))
    key_b = bytes(range(32, 64))
    # A bootloader trusting key_a means an SPKI whose sha256 IS key_a, which no
    # real key gives. So drive the comparison directly instead of forging one.
    spki = P256_SPKI_PREFIX + b"\x11" * (P256_SPKI_LEN - len(P256_SPKI_PREFIX))
    trusted = hashlib.sha256(spki).hexdigest()

    with tempfile.TemporaryDirectory() as d:
        bpath = os.path.join(d, "boot.bin")
        open(bpath, "wb").write(b"\x00" * 256 + spki + b"\x00" * 256)

        found = bootloader_keyhashes(open(bpath, "rb").read())
        if found == [trusted]:
            print("  ok   the bootloader's key is recovered from its binary")
        else:
            print(f"  FAIL bootloader key extraction: {found}")
            ok = False

        good = os.path.join(d, "good.bin")
        open(good, "wb").write(build_image(bytes.fromhex(trusted)))
        bad = os.path.join(d, "bad.bin")
        open(bad, "wb").write(build_image(key_b))

        h, err = image_keyhash(open(good, "rb").read())
        if h == trusted and err is None:
            print("  ok   a matching image reads back the same hash")
        else:
            print(f"  FAIL matching image: {h} {err}")
            ok = False

        h, err = image_keyhash(open(bad, "rb").read())
        if h == key_b.hex():
            print("  ok   a mismatched image reads back a different hash")
        else:
            print(f"  FAIL mismatched image: {h} {err}")
            ok = False

        unsigned = os.path.join(d, "unsigned.bin")
        open(unsigned, "wb").write(b"\xff" * 512)
        h, err = image_keyhash(open(unsigned, "rb").read())
        if h is None and err:
            print("  ok   an unsigned blob is refused, not passed")
        else:
            print(f"  FAIL unsigned blob accepted: {h}")
            ok = False

        _ = key_a

    print("keyhash-check self-test: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
