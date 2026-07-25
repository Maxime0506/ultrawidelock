#!/usr/bin/env python3
"""Presence-signed git tags: prove a human was physically present at a release.

A GPG signature proves WHO made a tag. It does not prove they were there: a
stolen key, a compromised CI runner or a coerced automated pipeline all produce
perfectly valid signatures. This adds the orthogonal claim -- that a provisioned
credential was physically within a few tens of centimetres of the machine when
the tag was made -- measured by UWB time-of-flight, which an attacker cannot
shorten by relaying.

    presence_git.py enroll --port /dev/tty.usbmodem... --name my-dongle
    presence_git.py sign   --tag v1.2.0 --port /dev/tty.usbmodem...
    presence_git.py verify --tag v1.2.0          # what CI runs

HOW THE NONCE WORKS, AND WHAT THAT COSTS
Everywhere else in this protocol the verifier mints a random nonce and remembers
it, which makes replay impossible. CI cannot do that: it was not present when
the tag was made, so it has no nonce to remember. The nonce is therefore DERIVED
from what is being signed:

    nonce = SHA-256("openaliro-presence-tag-v1\\0" + tag + "\\0" + commit)[:16]

so any verifier can recompute it. The assertion is then cryptographically bound
to exactly one (tag name, commit) pair and is worthless for any other.

The honest cost, which must not be buried: a derived nonce binds the proof to an
ARTEFACT, not to a MOMENT. The dongle has no trusted wall clock and reports
unix_ms = TIME_NONE, so a verifier cannot tell whether the presence happened
today or last year. Someone who obtained an assertion for this exact (tag,
commit) pair before it was published could publish that tag themselves without
being present. That window is narrow -- it requires the assertion before the tag
exists -- but it is real. It closes the day the dongle carries attested time,
which is precisely why unix_ms is a separate field in the wire format rather
than something derived from uptime.

Enrolled keys live in .presence/enrolled, committed to the repo so the set of
trusted dongles is reviewable in history like any other change. The tag carries
only a key id; the key material always comes from that file, because a tag that
carried its own public key would let anyone sign with a key they just made up.

Stdlib only, except that enroll/sign import pyserial lazily to talk to a real
dongle. verify -- the half CI runs -- needs no serial port and no extra package.
"""

import argparse
import hashlib
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import presence_verify as pv  # noqa: E402

# Domain separator. Keeps an assertion minted for a tag from being reusable in
# any other presence context, and vice versa.
TAG_DOMAIN = b"openaliro-presence-tag-v1"

ENROLLED_PATH = os.path.join(".presence", "enrolled")

TRAILER_KEY_ID = "Presence-Key-Id"
TRAILER_ASSERTION = "Presence-Assertion"

# Console protocol spoken by the dongle (ports/esp32/apps/reader/main/presence_link.c).
# Everything is a line: a command line in, one tagged hex line back. The dongle
# shares this console with the log stream and the rest of the shell, so answers are
# found by their tag rather than by position, and an interleaved log line is simply
# a line that does not match.
TAG_PUB = "PRESENCE-PUB"
TAG_P256 = "PRESENCE-P256"
TAG_ERR = "PRESENCE-ERR"

# Lines read while looking for a tagged answer. A boot banner plus a busy log stream
# stays well inside this; a port that is not running this firmware fails fast.
REPLY_LINE_BUDGET = 400


class PresenceError(RuntimeError):
    pass


def key_id(point: bytes) -> bytes:
    """Stable 8-byte id for a public key: first 8 bytes of SHA-256(point).

    Same construction as aliro_assert_cred_id() uses for credentials, so the two
    identifier schemes in this system read alike.
    """
    return hashlib.sha256(point).digest()[:8]


def binding_nonce(tag: str, commit: str) -> bytes:
    """The 16-byte challenge nonce a tag's assertion must echo."""
    h = hashlib.sha256()
    h.update(TAG_DOMAIN)
    h.update(b"\0")
    h.update(tag.encode())
    h.update(b"\0")
    h.update(commit.encode())
    return h.digest()[: pv.NONCE_LEN]


def git(*args, cwd=None) -> str:
    r = subprocess.run(["git", *args], capture_output=True, text=True, cwd=cwd)
    if r.returncode != 0:
        raise PresenceError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def tag_commit(tag: str, cwd=None) -> str:
    """The commit a tag resolves to, dereferencing annotated tags."""
    return git("rev-parse", f"{tag}^{{commit}}", cwd=cwd)


def tag_trailer(tag: str, key: str, cwd=None) -> str:
    """One trailer value from a tag message, or "" if absent.

    Uses git's own trailer parser rather than scanning the message, because an
    annotated tag may also carry a PGP signature block and hand-rolled scanning
    would have to know to step around it.
    """
    return git("tag", "-l", f"--format=%(trailers:key={key},valueonly,unfold)", tag, cwd=cwd)


def read_enrolled(path=ENROLLED_PATH, root=None) -> dict:
    """Load trusted dongle keys. Returns {key_id_hex: (name, point)}."""
    full = os.path.join(root, path) if root else path
    keys = {}
    if not os.path.exists(full):
        return keys
    with open(full, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2:
                raise PresenceError(f"{full}:{lineno}: expected '<name> <point-hex>'")
            name, point_hex = parts
            try:
                point = bytes.fromhex(point_hex)
            except ValueError as exc:
                raise PresenceError(f"{full}:{lineno}: bad hex for {name}") from exc
            if len(point) != pv.PUB_LEN or point[0] != 0x04:
                raise PresenceError(
                    f"{full}:{lineno}: {name} is not a {pv.PUB_LEN}-byte uncompressed point"
                )
            keys[key_id(point).hex()] = (name, point)
    return keys


def verify_tag(tag: str, max_cm=40, root=None, enrolled_path=ENROLLED_PATH, openssl="openssl"):
    """Verify a tag's presence assertion. Returns (verdict, detail-dict).

    A verdict of None means the tag carries no assertion at all, which is not a
    failure by itself -- callers decide whether an unsigned tag is acceptable.
    """
    commit = tag_commit(tag, cwd=root)
    kid = tag_trailer(tag, TRAILER_KEY_ID, cwd=root)
    frame_hex = tag_trailer(tag, TRAILER_ASSERTION, cwd=root)

    detail = {"tag": tag, "commit": commit, "key_id": kid}

    if not kid and not frame_hex:
        return None, detail
    if not kid or not frame_hex:
        raise PresenceError(
            f"{tag}: incomplete presence trailers "
            f"({TRAILER_KEY_ID}={kid or 'missing'}, {TRAILER_ASSERTION}="
            f"{'present' if frame_hex else 'missing'})"
        )

    try:
        frame = bytes.fromhex(frame_hex)
    except ValueError as exc:
        raise PresenceError(f"{tag}: {TRAILER_ASSERTION} is not hex") from exc

    keys = read_enrolled(enrolled_path, root=root)
    if kid not in keys:
        raise PresenceError(
            f"{tag}: signed by dongle {kid}, which is not enrolled in {enrolled_path}"
        )
    name, point = keys[kid]
    detail["dongle"] = name

    nonce = binding_nonce(tag, commit)
    detail["nonce"] = nonce.hex()

    verdict, fields = pv.verify(frame, point, nonce, max_cm=max_cm, openssl=openssl)
    detail["fields"] = fields
    return verdict, detail


def open_port(port: str, timeout=8.0):
    try:
        import serial  # pyserial, only needed to talk to a real dongle
    except ImportError as exc:
        raise PresenceError(
            "pyserial is required to talk to a dongle. Either 'pip install pyserial', "
            "or run this script under 'uv run --with pyserial'. Note that "
            "'uv tool install pyserial' does NOT work: it exposes only pyserial's "
            "own commands, not the importable library. 'verify' needs none of this."
        ) from exc
    try:
        return serial.Serial(port, 115200, timeout=timeout)
    except serial.SerialException as exc:
        # A wrong port name is the most common bring-up mistake by far; a
        # traceback here would look like a bug in the tool rather than a typo.
        raise PresenceError(f"cannot open {port}: {exc}") from exc


def read_line(ser) -> str:
    """Read one line, or raise on a silent port. Undecodable bytes are not fatal.

    Console output is not guaranteed to be clean UTF-8 -- a board reset mid-read
    puts ROM garbage on the wire -- and a decode error there should look like a
    line that does not match, not like a crash.
    """
    raw = ser.readline()
    if not raw:
        raise PresenceError("the port went silent (no newline before the timeout)")
    return raw.decode("utf-8", "replace").strip()


def ask(ser, command: str, tag: str, what: str) -> str:
    """Send a console command and return the payload of the first line tagged `tag`.

    Answers are located by tag rather than by position because the dongle's console
    also carries the log stream and the shell's own echo. That is the whole reason
    this protocol is lines of text: an interleaved log line is a line that does not
    match, where in a binary framing it was a corrupted response.
    """
    ser.reset_input_buffer()
    ser.write((command + "\n").encode())
    ser.flush()
    # A board lacking the command prints one "Unrecognized command" and then goes
    # quiet, so the timeout -- not the line budget -- is the path a real mistake
    # takes. Both ends carry the same advice or it would never be read.
    advice = (
        f"no {what} came back from '{command}'. Check the board was flashed with "
        "CONFIG_WOZ_PRESENCE=y (run 'help' on its console to see)."
    )
    for _ in range(REPLY_LINE_BUDGET):
        try:
            line = read_line(ser)
        except PresenceError as exc:
            raise PresenceError(advice) from exc
        if line.startswith(tag + " "):
            return line[len(tag) + 1 :].strip()
        if line.startswith(TAG_ERR + " "):
            raise PresenceError(f"dongle refused '{command}': {line[len(TAG_ERR) + 1:]}")
    raise PresenceError(advice)


def unhex(payload: str, n: int, what: str) -> bytes:
    try:
        raw = bytes.fromhex(payload)
    except ValueError as exc:
        raise PresenceError(f"{what} was not hex: {payload[:40]!r}") from exc
    if len(raw) != n:
        raise PresenceError(f"{what} was {len(raw)} bytes, expected {n}")
    return raw


# `aliro-import <hex>` has to fit the console's line buffer, which both apps set to
# this. A blob that overflows it would arrive truncated and be rejected as malformed,
# blaming the blob for what is really a transport limit, so check it here instead.
CONSOLE_LINE_MAX = 1024

# The identity blob is printed on a bare line with no tag, unlike the presence
# replies. A long unbroken run of hex is a strong enough signature, and demanding a
# minimum length keeps a short hex-ish log line from matching. 64 chars is well under
# the smallest real blob and well over anything that shows up in a log line.
BLOB_MIN_HEX = 64


def read_bare_hex(ser, min_chars: int, what: str) -> str:
    # As in ask(): a board without the command answers once and falls silent, so the
    # timeout carries the same advice as running out of lines.
    advice = (
        f"no {what} came back. Was this board built with CONFIG_WOZ_ALIRO_CLONE=y? "
        "(run 'help' on its console to see)"
    )
    for _ in range(REPLY_LINE_BUDGET):
        try:
            candidate = read_line(ser).strip()
        except PresenceError as exc:
            raise PresenceError(advice) from exc
        if len(candidate) >= min_chars and len(candidate) % 2 == 0:
            try:
                bytes.fromhex(candidate)
            except ValueError:
                continue
            return candidate
    raise PresenceError(advice)


def export_identity(ser) -> str:
    """Read a reader identity + trust blob from a provisioned board, as hex."""
    ser.reset_input_buffer()
    ser.write(b"aliro-export\n")
    ser.flush()
    return read_bare_hex(ser, BLOB_MIN_HEX, "identity blob")


def import_identity(ser, blob_hex: str) -> str:
    """Load an identity blob into a board. Returns the console's confirmation line."""
    command = f"aliro-import {blob_hex}"
    if len(command) + 1 > CONSOLE_LINE_MAX:
        raise PresenceError(
            f"identity blob is {len(blob_hex) // 2} bytes, which does not fit the "
            f"{CONSOLE_LINE_MAX}-char console line. Raise max_cmdline_length in both "
            "app shells, or move the transfer off the command line."
        )
    ser.reset_input_buffer()
    ser.write((command + "\n").encode())
    ser.flush()
    for _ in range(REPLY_LINE_BUDGET):
        line = read_line(ser)
        if line.startswith("aliro-import:"):
            return line
    raise PresenceError("the destination board never acknowledged the import")


def dongle_pubkey(ser) -> bytes:
    point = unhex(ask(ser, "presence pub", TAG_PUB, "public key"), pv.PUB_LEN, "public key")
    if point[0] != 0x04 or point == b"\x00" * pv.PUB_LEN:
        raise PresenceError("dongle has no signing key (it returned an unusable point)")
    return point


def dongle_assert(ser, nonce: bytes) -> bytes:
    payload = ask(ser, f"presence assert {nonce.hex()}", TAG_P256, "assertion")
    return unhex(payload, pv.WIRE_P256, "assertion")


def cmd_nonce(args) -> int:
    commit = args.commit or tag_commit(args.tag)
    print(binding_nonce(args.tag, commit).hex())
    return 0


# Verdicts that can only be reached once the signature has already verified.
# Everything else means the frame itself was rejected.
AUTHENTIC_VERDICTS = (pv.OK, pv.E_NONCE, pv.E_STALE, pv.E_ABSENT, pv.E_RANGE)


def cmd_probe(args) -> int:
    """Bring-up check: does this dongle produce a frame the verifier accepts?

    Separates the two questions a first flash needs answered, which the normal
    verdict conflates. "Signature verified but ABSENT" means the whole crypto
    chain works and merely no phone has ranged -- a pass. "Signature failed"
    means keygen, signing or framing is broken -- the real failure.
    """
    ser = open_port(args.port)
    try:
        point = dongle_pubkey(ser)
        nonce = os.urandom(pv.NONCE_LEN)
        frame = dongle_assert(ser, nonce)
    finally:
        ser.close()

    verdict, fields = pv.verify(frame, point, nonce, max_cm=args.max_cm, openssl=args.openssl)
    authentic = verdict in AUTHENTIC_VERDICTS

    print(f"pubkey     {point.hex()}")
    print(f"key id     {key_id(point).hex()}")
    print(f"frame      {len(frame)} bytes")
    print(f"signature  {'VERIFIED' if authentic else 'FAILED'}")
    print(f"presence   {pv.VERDICT_NAME[verdict]}: {pv.VERDICT_REASON[verdict]}")
    if fields is not None:
        dist = fields["distance_cm"]
        shown = "none" if dist == pv.DIST_NONE else f"{dist} cm"
        print(f"distance   {shown}")
        print(f"uptime     {fields['uptime_ms']} ms")
        print(f"cred id    {fields['cred_id'].hex()}")

    if not authentic:
        return 1
    if verdict != pv.OK:
        print("\nCrypto chain is good. Presence itself was not established — "
              "wake the phone and hold it near the dongle, then run this again.")
    return 0


def cmd_enroll(args) -> int:
    ser = open_port(args.port)
    try:
        point = dongle_pubkey(ser)
    finally:
        ser.close()

    kid = key_id(point).hex()
    os.makedirs(os.path.dirname(args.file) or ".", exist_ok=True)
    existing = read_enrolled(args.file)
    if kid in existing:
        print(f"already enrolled as {existing[kid][0]} ({kid})")
        return 0
    new = not os.path.exists(args.file)
    with open(args.file, "a", encoding="utf-8") as fh:
        if new:
            fh.write("# Trusted presence dongles: <name> <65-byte uncompressed P-256 point>\n")
            fh.write("# Reviewed in history like any other change; a tag names only a key id.\n")
        fh.write(f"{args.name} {point.hex()}\n")
    print(f"enrolled {args.name} as {kid} in {args.file}")
    return 0


def cmd_clone(args) -> int:
    """Copy a provisioned reader identity onto the dongle over two serial ports.

    This is what lets a phone's EXISTING Wallet credential transact with the dongle:
    the credential was issued against a particular reader identity, so the dongle has
    to present that same identity rather than be enrolled separately. The blob
    carries the reader private key, which is exactly why the console command behind
    it is not compiled in by default.
    """
    src = open_port(args.source)
    try:
        blob = export_identity(src)
    finally:
        src.close()
    print(f"exported {len(blob) // 2} bytes of identity + trust from {args.source}")

    dst = open_port(args.port)
    try:
        line = import_identity(dst, blob)
    finally:
        dst.close()
    print(line)
    if "adopted" not in line:
        return 1
    print("the dongle now presents the source board's reader identity")
    return 0


def cmd_sign(args) -> int:
    commit = args.commit or git("rev-parse", "HEAD")
    nonce = binding_nonce(args.tag, commit)

    ser = open_port(args.port)
    try:
        point = dongle_pubkey(ser)
        frame = dongle_assert(ser, nonce)
    finally:
        ser.close()

    # Verify before tagging. A tag carrying an assertion that does not verify is
    # worse than an unsigned one: it looks like a proof to anyone who does not
    # re-check it.
    verdict, fields = pv.verify(frame, point, nonce, max_cm=args.max_cm, openssl=args.openssl)
    if verdict != pv.OK:
        print(f"presence not established: {pv.VERDICT_NAME[verdict]}: "
              f"{pv.VERDICT_REASON[verdict]}", file=sys.stderr)
        return 1

    body = args.message or f"Release {args.tag}"
    msg = (
        f"{body}\n\n"
        f"{TRAILER_KEY_ID}: {key_id(point).hex()}\n"
        f"{TRAILER_ASSERTION}: {frame.hex()}\n"
    )
    git("tag", "-a", args.tag, commit, "-m", msg)
    print(f"tagged {args.tag} at {commit[:12]}: present at {fields['distance_cm']} cm")
    return 0


def cmd_verify(args) -> int:
    verdict, detail = verify_tag(
        args.tag, max_cm=args.max_cm, enrolled_path=args.file, openssl=args.openssl
    )
    if verdict is None:
        if args.require:
            print(f"{args.tag}: no presence assertion, and --require was given", file=sys.stderr)
            return 1
        print(f"{args.tag}: not presence-signed (skipping)")
        return 0
    if verdict != pv.OK:
        print(f"{args.tag}: REJECT {pv.VERDICT_NAME[verdict]}: {pv.VERDICT_REASON[verdict]}",
              file=sys.stderr)
        return 1
    f = detail["fields"]
    print(
        f"{args.tag}: presence verified — dongle {detail['dongle']} ({detail['key_id']}), "
        f"credential {f['cred_id'].hex()}, {f['distance_cm']} cm, commit {detail['commit'][:12]}"
    )
    return 0


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("nonce", help="print the binding nonce for a tag")
    p.add_argument("--tag", required=True)
    p.add_argument("--commit", help="defaults to what the tag resolves to")
    p.set_defaults(func=cmd_nonce)

    p = sub.add_parser("probe", help="bring-up check against a connected dongle")
    p.add_argument("--port", required=True, help="dongle serial port")
    p.add_argument("--max-cm", type=int, default=40)
    p.add_argument("--openssl", default="openssl")
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("enroll", help="record a dongle's public key as trusted")
    p.add_argument("--port", required=True, help="dongle serial port")
    p.add_argument("--name", required=True, help="human label for this dongle")
    p.add_argument("--file", default=ENROLLED_PATH)
    p.set_defaults(func=cmd_enroll)

    p = sub.add_parser("clone", help="copy a provisioned reader identity onto the dongle")
    p.add_argument("--source", required=True, help="serial port of the provisioned board")
    p.add_argument("--port", required=True, help="dongle serial port")
    p.set_defaults(func=cmd_clone)

    p = sub.add_parser("sign", help="create a presence-signed annotated tag")
    p.add_argument("--tag", required=True)
    p.add_argument("--port", required=True, help="dongle serial port")
    p.add_argument("--commit", help="defaults to HEAD")
    p.add_argument("-m", "--message", help="tag message body")
    p.add_argument("--max-cm", type=int, default=40)
    p.add_argument("--openssl", default="openssl")
    p.set_defaults(func=cmd_sign)

    p = sub.add_parser("verify", help="verify a tag's presence assertion")
    p.add_argument("--tag", required=True)
    p.add_argument("--max-cm", type=int, default=40)
    p.add_argument("--file", default=ENROLLED_PATH)
    p.add_argument("--openssl", default="openssl")
    p.add_argument("--require", action="store_true",
                   help="fail when the tag carries no assertion at all")
    p.set_defaults(func=cmd_verify)
    return ap


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except PresenceError as exc:
        print(f"presence: {exc}", file=sys.stderr)
        return 1
    except pv.OpensslMissing as exc:
        print(f"presence: cannot verify: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
