#!/usr/bin/env python3
"""Verify an ECDSA-P256 presence assertion against a dongle's public key.

This is the portable half of the presence primitive. A P-256 assertion is
checkable by any holder of the dongle's public point, which is what lets a CI
job, a release-tag hook or a second reviewer accept "a human was physically at
that machine" without being trusted with a secret. A shared-secret assertion
could only be checked by a holder of the key that could equally forge it, which
is why that mode was retired rather than kept alongside this one.

Stdlib only. The curve arithmetic is delegated to the openssl binary already
present on any machine that can run a CI job, so nothing is vendored and no
third-party Python package is required. That also keeps this file honest about
what it does NOT do: it never implements P-256 itself.

The wire format is defined by modules/woz_aliro/src/aliro_assert.c, and the
verdicts and their ORDER mirror aliro_assert_verify_p256() exactly, so the two
implementations cannot disagree about what a frame means. The offsets below are
drift-gated against that C source by tests/host/test_presence_verify.py.

A distance is only worth as much as the measurement behind it, so the frame also
carries that measurement's integrity evidence and this verifier refuses a frame
that does not claim a well-correlated STS. Rejecting is the whole point: an
undefended 19 cm and a defended 19 cm arrive as the same integer, so a verifier
that ignores the evidence cannot tell a measurement from an assertion.

Usage:
    presence_verify.py --pubkey <hex|file> --frame <hex|file> --nonce <hex>
                       --cred-id <hex> [--max-cm N] [--min-uptime-ms N]
                       [--min-sts-quality N] [--json]

Exit status is 0 only when presence is confirmed, so it can gate a command
directly. Every rejection exits 1 and names its reason.
"""

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile

# Frame layout. Mirrors the OFF_* defines in modules/woz_aliro/src/aliro_assert.c;
# multi-byte fields are big-endian.
OFF_MAGIC = 0
OFF_VERSION = 2
OFF_ALG = 3
OFF_STATUS = 4
OFF_NONCE = 5
OFF_CREDID = 21
OFF_DISTANCE = 29
OFF_RANGE_FLAGS = 31
OFF_STS_QUALITY = 32
OFF_TRUST = 34
OFF_UPTIME = 35
OFF_UNIX = 43
OFF_TAG = 51

MAGIC = b"\xa1\x50"
VERSION = 3

NONCE_LEN = 16
CREDID_LEN = 8
SIG_LEN = 64
PUB_LEN = 65

SIGNED_LEN = 51
WIRE_P256 = SIGNED_LEN + SIG_LEN  # 115

# range_flags bits. STS_OK means every block in the agreeing run behind
# distance_cm correlated its scrambled timestamp sequence well enough to trust.
# Bits outside FLAGS_KNOWN are rejected rather than ignored: an unknown bit is a
# claim this verifier cannot evaluate, and a security field it cannot read has
# to fail closed.
RANGE_STS_OK = 0x01
RANGE_FLAGS_KNOWN = 0x01

# 1 was HMAC-SHA256, retired with the paired-host PAM path. Never reused: a v1
# frame must reject as unknown-alg rather than be read under another scheme.
ALG_ECDSA_P256 = 2

DIST_NONE = 0xFFFF
TIME_NONE = 0

PRESENCE_ABSENT = 0
PRESENCE_PRESENT = 1

# Verdicts, mirroring enum aliro_assert_verdict.
OK = 0
E_MALFORMED = -1
E_MAC = -2
E_NONCE = -3
E_STALE = -4
E_ABSENT = -5
E_RANGE = -6
E_ALG = -7
E_CREDENTIAL = -8
E_INTEGRITY = -9

VERDICT_NAME = {
    OK: "OK",
    E_MALFORMED: "E_MALFORMED",
    E_MAC: "E_MAC",
    E_NONCE: "E_NONCE",
    E_STALE: "E_STALE",
    E_ABSENT: "E_ABSENT",
    E_RANGE: "E_RANGE",
    E_ALG: "E_ALG",
    E_CREDENTIAL: "E_CREDENTIAL",
    E_INTEGRITY: "E_INTEGRITY",
}

VERDICT_REASON = {
    OK: "presence confirmed",
    E_MALFORMED: "bad length, magic or version",
    E_MAC: "signature does not verify: wrong key or tampered frame",
    E_NONCE: "nonce does not echo the challenge: replay or mismatch",
    E_STALE: "dongle uptime did not move forward",
    E_ABSENT: "dongle reports no credential present",
    E_RANGE: "no range, or further away than the threshold",
    E_ALG: "not an ECDSA-P256 frame",
    E_CREDENTIAL: "credential is not the enrolled human",
    E_INTEGRITY: "distance is not backed by a well-correlated STS: not a measurement to trust",
}

# The fixed SubjectPublicKeyInfo header for an uncompressed id-ecPublicKey point
# on prime256v1. Prepending it to the 65-byte point is the whole of the DER
# encoding, so no ASN.1 library is needed to hand openssl a public key.
SPKI_P256_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")


class OpensslMissing(RuntimeError):
    """Raised when the openssl binary cannot be run.

    Deliberately not folded into a verification failure: "we could not check"
    and "the signature is bad" are different facts, and silently reporting the
    first as the second would turn a broken install into a security verdict.
    """


def der_uint(v: bytes) -> bytes:
    """DER-encode a big-endian unsigned integer as an ASN.1 INTEGER."""
    v = v.lstrip(b"\x00") or b"\x00"
    if v[0] & 0x80:
        # DER integers are signed, so a leading high bit needs a zero pad or it
        # would decode as negative.
        v = b"\x00" + v
    return b"\x02" + bytes([len(v)]) + v


def raw_sig_to_der(sig: bytes) -> bytes:
    """Convert a raw r||s ECDSA signature to the DER form openssl expects."""
    if len(sig) != SIG_LEN:
        raise ValueError(f"signature must be {SIG_LEN} bytes, got {len(sig)}")
    body = der_uint(sig[:32]) + der_uint(sig[32:])
    # Both integers are at most 33 bytes, so the sequence body never reaches the
    # 128-byte long-form length threshold.
    return b"\x30" + bytes([len(body)]) + body


def point_to_pem(point: bytes) -> bytes:
    """Wrap a 65-byte uncompressed P-256 point as a PEM public key."""
    if len(point) != PUB_LEN:
        raise ValueError(f"public key must be {PUB_LEN} bytes, got {len(point)}")
    if point[0] != 0x04:
        raise ValueError("public key is not an uncompressed point (no 0x04 prefix)")
    der = SPKI_P256_PREFIX + point
    b64 = base64.encodebytes(der).decode().strip()
    return f"-----BEGIN PUBLIC KEY-----\n{b64}\n-----END PUBLIC KEY-----\n".encode()


def verify_signature(point: bytes, msg: bytes, sig: bytes, openssl: str = "openssl") -> bool:
    """True when sig is a valid ECDSA-P256-SHA256 signature over msg under point.

    Returns False for a key that is structurally unusable -- notably the all-zero
    point a dongle reports when it has no signing key -- because such a key must
    never verify anything.
    """
    try:
        pem = point_to_pem(point)
        der = raw_sig_to_der(sig)
    except ValueError:
        return False

    with tempfile.TemporaryDirectory() as d:
        pub_path = os.path.join(d, "pub.pem")
        sig_path = os.path.join(d, "sig.der")
        msg_path = os.path.join(d, "msg.bin")
        for path, data in ((pub_path, pem), (sig_path, der), (msg_path, msg)):
            with open(path, "wb") as fh:
                fh.write(data)
        try:
            r = subprocess.run(
                [openssl, "dgst", "-sha256", "-verify", pub_path, "-signature", sig_path, msg_path],
                capture_output=True,
            )
        except (OSError, FileNotFoundError) as exc:
            raise OpensslMissing(f"cannot run {openssl!r}: {exc}") from exc
    return r.returncode == 0


def parse_fields(wire: bytes) -> dict:
    """Decode the assertion body. Assumes framing has already been checked.

    Parsing before authentication is intentional and matches the C: the fields
    are returned for logging even on a reject, and the caller is responsible for
    ignoring them unless the verdict is OK.
    """
    return {
        "alg": wire[OFF_ALG],
        "status": wire[OFF_STATUS],
        "nonce": wire[OFF_NONCE : OFF_NONCE + NONCE_LEN],
        "cred_id": wire[OFF_CREDID : OFF_CREDID + CREDID_LEN],
        "distance_cm": int.from_bytes(wire[OFF_DISTANCE : OFF_DISTANCE + 2], "big"),
        "range_flags": wire[OFF_RANGE_FLAGS],
        "sts_quality": int.from_bytes(
            wire[OFF_STS_QUALITY : OFF_STS_QUALITY + 2], "big", signed=True
        ),
        "trust_level": wire[OFF_TRUST],
        "uptime_ms": int.from_bytes(wire[OFF_UPTIME : OFF_UPTIME + 8], "big"),
        "unix_ms": int.from_bytes(wire[OFF_UNIX : OFF_UNIX + 8], "big"),
    }


def check_framing(wire: bytes, want_alg: int = ALG_ECDSA_P256) -> int:
    """Length, magic, version and algorithm checks, in the C's exact order.

    Algorithm is tested before length on purpose: a well-formed frame for the
    other algorithm is a misconfigured peer, not corrupt framing, and the two
    have entirely different fixes.
    """
    if wire is None or len(wire) < SIGNED_LEN:
        return E_MALFORMED
    if wire[OFF_MAGIC : OFF_MAGIC + 2] != MAGIC or wire[OFF_VERSION] != VERSION:
        return E_MALFORMED
    if wire[OFF_ALG] != want_alg:
        return E_ALG
    if len(wire) != WIRE_P256:
        return E_MALFORMED
    return OK


def verify(
    wire: bytes,
    pubkey: bytes,
    expected_nonce: bytes,
    expected_cred_id: bytes,
    max_cm: int = 40,
    min_uptime_ms: int = 0,
    min_sts_quality: int = 0,
    openssl: str = "openssl",
):
    """Fully verify a P-256 assertion. Returns (verdict, fields-or-None).

    Check order mirrors aliro_assert_verify_p256(): framing, algorithm,
    signature, nonce echo, enrolled credential, forward progress, presence,
    distance, then range integrity. Fields are returned only once the signature
    has passed, so nothing a forger wrote can reach a caller that logs them.

    min_sts_quality is the one policy knob the C verifier deliberately does not
    have. The producer already applied its own compiled-in floor, so 0 changes
    nothing; raising it lets a verifier demand better correlation than the
    firmware was built with, which is how a floor gets tightened from bench
    captures without reflashing every board first. trust_level is reported but
    not thresholded, because the firmware only ever emits ranges that already
    reached its consensus K -- a knob with no reachable range is not a knob.
    """
    fr = check_framing(wire)
    if fr != OK:
        return fr, None

    if not verify_signature(pubkey, wire[:OFF_TAG], wire[OFF_TAG:], openssl=openssl):
        return E_MAC, None

    f = parse_fields(wire)

    if expected_nonce is None or f["nonce"] != expected_nonce:
        return E_NONCE, f
    if expected_cred_id is None or f["cred_id"] != expected_cred_id:
        return E_CREDENTIAL, f
    if min_uptime_ms != 0 and f["uptime_ms"] <= min_uptime_ms:
        return E_STALE, f
    if f["status"] != PRESENCE_PRESENT:
        return E_ABSENT, f
    if f["distance_cm"] == DIST_NONE or f["distance_cm"] > max_cm:
        return E_RANGE, f
    if f["range_flags"] & ~RANGE_FLAGS_KNOWN:
        return E_INTEGRITY, f
    if not f["range_flags"] & RANGE_STS_OK:
        return E_INTEGRITY, f
    # Beyond the C, which has no business holding a tunable radio threshold.
    if f["sts_quality"] < min_sts_quality:
        return E_INTEGRITY, f
    return OK, f


def read_bytes(arg: str, name: str) -> bytes:
    """Accept either a hex string or a path to a file of hex or raw bytes."""
    if os.path.exists(arg):
        with open(arg, "rb") as fh:
            data = fh.read()
        try:
            return bytes.fromhex(data.decode("ascii").strip())
        except (UnicodeDecodeError, ValueError):
            return data
    try:
        return bytes.fromhex(arg.strip())
    except ValueError:
        raise SystemExit(f"{name}: not a hex string and not an existing file")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Verify an ECDSA-P256 presence assertion from an Aliro presence dongle."
    )
    ap.add_argument("--pubkey", required=True, help="dongle public key: 65-byte point, hex or file")
    ap.add_argument("--frame", required=True, help="assertion frame: 115 bytes, hex or file")
    ap.add_argument(
        "--nonce",
        required=True,
        help="the 16-byte challenge nonce, hex. Required: replay protection rests entirely on it",
    )
    ap.add_argument(
        "--cred-id",
        required=True,
        help="the enrolled 8-byte credential id, hex; pins the proof to one human",
    )
    ap.add_argument(
        "--max-cm",
        type=int,
        default=40,
        help="accept at or below this distance in cm (default 40, matching the host config default)",
    )
    ap.add_argument(
        "--min-uptime-ms",
        type=int,
        default=0,
        help="require dongle uptime strictly greater than this (0 disables the check)",
    )
    ap.add_argument(
        "--min-sts-quality",
        type=int,
        default=0,
        help="require an STS quality index at or above this (0 = the producer's own floor)",
    )
    ap.add_argument("--openssl", default="openssl", help="openssl binary to use")
    ap.add_argument("--json", action="store_true", help="emit the verdict and fields as JSON")
    args = ap.parse_args(argv)

    pubkey = read_bytes(args.pubkey, "--pubkey")
    wire = read_bytes(args.frame, "--frame")
    nonce = read_bytes(args.nonce, "--nonce")
    cred_id = read_bytes(args.cred_id, "--cred-id")
    if len(nonce) != NONCE_LEN:
        raise SystemExit(f"--nonce: expected {NONCE_LEN} bytes, got {len(nonce)}")
    if len(cred_id) != CREDID_LEN:
        raise SystemExit(f"--cred-id: expected {CREDID_LEN} bytes, got {len(cred_id)}")

    try:
        verdict, f = verify(
            wire,
            pubkey,
            nonce,
            cred_id,
            args.max_cm,
            args.min_uptime_ms,
            args.min_sts_quality,
            openssl=args.openssl,
        )
    except OpensslMissing as exc:
        print(f"presence: cannot verify: {exc}", file=sys.stderr)
        return 2

    if args.json:
        out = {"verdict": VERDICT_NAME[verdict], "code": verdict, "reason": VERDICT_REASON[verdict]}
        if f is not None:
            out["fields"] = {
                "alg": f["alg"],
                "status": f["status"],
                "cred_id": f["cred_id"].hex(),
                "distance_cm": f["distance_cm"],
                "range_flags": f["range_flags"],
                "sts_ok": bool(f["range_flags"] & RANGE_STS_OK),
                "sts_quality": f["sts_quality"],
                "trust_level": f["trust_level"],
                "uptime_ms": f["uptime_ms"],
                "unix_ms": f["unix_ms"],
            }
        print(json.dumps(out, indent=2))
    elif verdict == OK:
        print(
            f"PRESENT  cred_id={f['cred_id'].hex()}  {f['distance_cm']} cm "
            f"(threshold {args.max_cm})  uptime={f['uptime_ms']} ms\n"
            f"         STS ok  quality={f['sts_quality']}  "
            f"consensus={f['trust_level']} blocks"
        )
    else:
        print(f"REJECT   {VERDICT_NAME[verdict]}: {VERDICT_REASON[verdict]}")

    return 0 if verdict == OK else 1


if __name__ == "__main__":
    sys.exit(main())
