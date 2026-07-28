#!/usr/bin/env python3
"""Unit tests for tools/presence_verify.py.

Stdlib only; run directly or via tests/host/run.sh.

Two things are being pinned, and they fail for different reasons:

Drift.  Every constant the verifier depends on -- field offsets, field widths,
magic, version, algorithm ids, verdict codes -- is re-read from
modules/woz_aliro/src/aliro_assert.c and aliro_assert.h and compared. A verifier
written in a different language to the producer will otherwise rot silently:
change an offset in the C and this Python keeps parsing, just wrongly, and a
wrongly-parsed distance is an unlock decision. This is the same drift-gate
pattern test_mqtt_bridge.py uses against the firmware format string.

Curve.  The frame below carries a real ECDSA-P256 signature produced by openssl
over a fixed 51-byte prefix, so it pins the exact bytes a verifier must accept.
The C host suite can only test its P-256 path against a fake curve double
(ports/esp32/test/aliro_prim_host.c), so this file is the only place in the repo
where a presence assertion meets real curve arithmetic.
"""

import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import presence_verify as pv  # noqa: E402

ASSERT_C = os.path.join(ROOT, "modules", "woz_aliro", "src", "aliro_assert.c")
ASSERT_H = os.path.join(ROOT, "modules", "woz_aliro", "include", "aliro_assert.h")

# A fixed, real P-256 assertion. Field values match the known-answer vector in
# tests/host/test_aliro_assert.c, so both vectors describe the same assertion:
# PRESENT, 25 cm, STS ok, quality 300, consensus 3, uptime 1000000 ms.
KAT_POINT = bytes.fromhex(
    "04309fb5aa9caabaa07d83c7632818fd630968f6a087bd33d1d468fe0dd96b48"
    "6f0556f2823758223ca60d571632f36a36dcda48541f744cc6c514016504a1e5"
    "ee"
)
KAT_FRAME = bytes.fromhex(
    "a150030201deadbeef01020304a5a55a5a10203040c0c1c2c3c4c5c6c7001901"
    "012c0300000000000f42400000019f9a4a7a0052c68c6a1f8daff1f20f5e8f1b"
    "6035d55b1f2493760299c8ffcf2ececbe58115cd0e678e39f958a4aa781562b0"
    "ada8e9b7c0f62b7c3bf80d27e3d1de8d268782"
)
KAT_NONCE = bytes.fromhex("deadbeef01020304a5a55a5a10203040")
KAT_CREDID = bytes.fromhex("c0c1c2c3c4c5c6c7")


def have_openssl():
    try:
        subprocess.run(["openssl", "version"], capture_output=True, check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


HAVE_OPENSSL = have_openssl()
needs_openssl = unittest.skipUnless(HAVE_OPENSSL, "openssl binary not available")


def slurp(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def c_defines(path):
    """Numeric #define values from a C source file."""
    out = {}
    for name, val in re.findall(r"^#define\s+(\w+)\s+(0[xX][0-9a-fA-F]+|\d+)[uU]?\s*(?:/\*|//|$)",
                                slurp(path), re.M):
        out[name] = int(val, 0)
    return out


def c_enum_values(path):
    """Named enum constants with explicit integer initialisers."""
    return {n: int(v, 0)
            for n, v in re.findall(r"^\s*(\w+)\s*=\s*(-?\d+)\s*,", slurp(path), re.M)}


def build_prefix(status=1, nonce=KAT_NONCE, cred_id=KAT_CREDID, distance=25,
                 range_flags=pv.RANGE_STS_OK, sts_quality=300, trust_level=3,
                 uptime=1000000, unix_ms=1785000000000, alg=pv.ALG_ECDSA_P256, version=3):
    """Assemble a 51-byte signed prefix, mirroring put_prefix() in the C."""
    return (
        pv.MAGIC
        + bytes([version, alg, status])
        + nonce
        + cred_id
        + distance.to_bytes(2, "big")
        + bytes([range_flags])
        + sts_quality.to_bytes(2, "big", signed=True)
        + bytes([trust_level])
        + uptime.to_bytes(8, "big")
        + unix_ms.to_bytes(8, "big")
    )


def gen_key_and_sign(prefix):
    """Fresh P-256 key via openssl; returns (65-byte point, raw r||s over prefix)."""
    with tempfile.TemporaryDirectory() as d:
        key = os.path.join(d, "k.pem")
        subprocess.run(
            ["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key],
            check=True, capture_output=True,
        )
        txt = subprocess.run(
            ["openssl", "ec", "-in", key, "-pubout", "-text", "-noout"],
            capture_output=True, text=True, check=True,
        ).stdout
        hexpart = txt.split("pub:")[1].split("ASN1")[0]
        point = bytes.fromhex("".join(c for c in hexpart if c in "0123456789abcdef"))

        msg = os.path.join(d, "m.bin")
        with open(msg, "wb") as fh:
            fh.write(prefix)
        sig = os.path.join(d, "s.der")
        subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, "-out", sig, msg],
                       check=True, capture_output=True)
        with open(sig, "rb") as fh:
            der = fh.read()

    # DER SEQUENCE { INTEGER r, INTEGER s } back to raw r||s.
    i, raw = 2, b""
    for _ in range(2):
        ln = der[i + 1]
        raw += der[i + 2 : i + 2 + ln].lstrip(b"\x00").rjust(32, b"\x00")
        i += 2 + ln
    return point, raw


@contextlib.contextmanager
def quiet():
    """Swallow the CLI's output so it does not pollute the suite output."""
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        yield


class DriftTests(unittest.TestCase):
    """Every constant re-read from the C that defines the wire."""

    def setUp(self):
        self.dc = c_defines(ASSERT_C)
        self.dh = c_defines(ASSERT_H)
        self.enums = c_enum_values(ASSERT_H)

    def test_field_offsets(self):
        for name in ("MAGIC", "VERSION", "ALG", "STATUS", "NONCE", "CREDID",
                     "DISTANCE", "RANGE_FLAGS", "STS_QUALITY", "TRUST",
                     "UPTIME", "UNIX"):
            with self.subTest(field=name):
                self.assertEqual(getattr(pv, f"OFF_{name}"), self.dc[f"OFF_{name}"])

    def test_tag_offset_is_the_signed_length(self):
        # OFF_TAG is defined as ALIRO_ASSERT_SIGNED_LEN in the C, so it is checked
        # through that identity rather than as a literal.
        self.assertRegex(slurp(ASSERT_C), r"#define\s+OFF_TAG\s+ALIRO_ASSERT_SIGNED_LEN")
        self.assertEqual(pv.OFF_TAG, pv.SIGNED_LEN)

    def test_field_widths(self):
        for py, c in (("NONCE_LEN", "ALIRO_ASSERT_NONCE_LEN"),
                      ("CREDID_LEN", "ALIRO_ASSERT_CREDID_LEN"),
                      ("SIG_LEN", "ALIRO_ASSERT_SIG_LEN"),
                      ("PUB_LEN", "ALIRO_ASSERT_PUB_LEN"),
                      ("SIGNED_LEN", "ALIRO_ASSERT_SIGNED_LEN")):
            with self.subTest(field=py):
                self.assertEqual(getattr(pv, py), self.dh[c])

    def test_frame_lengths_follow_from_the_widths(self):
        self.assertEqual(pv.WIRE_P256, 115)

    def test_range_flag_bits(self):
        self.assertEqual(pv.RANGE_STS_OK, self.dh["ALIRO_ASSERT_RANGE_STS_OK"])
        self.assertEqual(pv.RANGE_FLAGS_KNOWN, self.dh["ALIRO_ASSERT_RANGE_FLAGS_KNOWN"])

    def test_every_known_flag_bit_is_named(self):
        # A bit inside FLAGS_KNOWN that no constant names would be silently
        # accepted by the mask check while meaning nothing to either verifier.
        named = pv.RANGE_STS_OK
        self.assertEqual(pv.RANGE_FLAGS_KNOWN, named)

    def test_magic_and_version(self):
        self.assertEqual(pv.MAGIC, bytes([self.dc["ASSERT_MAGIC0"], self.dc["ASSERT_MAGIC1"]]))
        self.assertEqual(pv.VERSION, self.dc["ASSERT_VERSION"])

    def test_algorithm_ids(self):
        self.assertEqual(pv.ALG_ECDSA_P256, self.enums["ALIRO_ASSERT_ALG_ECDSA_P256"])

    def test_retired_algorithm_id_is_not_reused(self):
        # 1 was HMAC-SHA256. If it ever reappears as an ALGORITHM id under another
        # name, a v1 frame stops being rejected and starts being reinterpreted.
        # Scoped to the alg enum: 1 is a valid value elsewhere (PRESENCE_PRESENT).
        algs = {n: v for n, v in self.enums.items() if n.startswith("ALIRO_ASSERT_ALG_")}
        self.assertNotIn(1, algs.values())
        self.assertEqual(algs, {"ALIRO_ASSERT_ALG_ECDSA_P256": 2})

    def test_status_values(self):
        self.assertEqual(pv.PRESENCE_ABSENT, self.enums["ALIRO_PRESENCE_ABSENT"])
        self.assertEqual(pv.PRESENCE_PRESENT, self.enums["ALIRO_PRESENCE_PRESENT"])

    def test_verdict_codes(self):
        for py, c in (("OK", "ALIRO_ASSERT_OK"),
                      ("E_MALFORMED", "ALIRO_ASSERT_E_MALFORMED"),
                      ("E_MAC", "ALIRO_ASSERT_E_MAC"),
                      ("E_NONCE", "ALIRO_ASSERT_E_NONCE"),
                      ("E_STALE", "ALIRO_ASSERT_E_STALE"),
                      ("E_ABSENT", "ALIRO_ASSERT_E_ABSENT"),
                      ("E_RANGE", "ALIRO_ASSERT_E_RANGE"),
                      ("E_ALG", "ALIRO_ASSERT_E_ALG"),
                      ("E_CREDENTIAL", "ALIRO_ASSERT_E_CREDENTIAL"),
                      ("E_INTEGRITY", "ALIRO_ASSERT_E_INTEGRITY")):
            with self.subTest(verdict=py):
                self.assertEqual(getattr(pv, py), self.enums[c])

    def test_every_verdict_has_a_name_and_a_reason(self):
        for code in (pv.OK, pv.E_MALFORMED, pv.E_MAC, pv.E_NONCE,
                     pv.E_STALE, pv.E_ABSENT, pv.E_RANGE, pv.E_ALG,
                     pv.E_CREDENTIAL, pv.E_INTEGRITY):
            self.assertIn(code, pv.VERDICT_NAME)
            self.assertIn(code, pv.VERDICT_REASON)


class DerTests(unittest.TestCase):
    def test_high_bit_gets_a_zero_pad(self):
        # Without the pad, DER would decode this as a negative integer.
        self.assertEqual(pv.der_uint(b"\x80\x01"), b"\x02\x03\x00\x80\x01")

    def test_leading_zeros_are_stripped(self):
        self.assertEqual(pv.der_uint(b"\x00\x00\x7f"), b"\x02\x01\x7f")

    def test_all_zero_encodes_as_zero(self):
        self.assertEqual(pv.der_uint(b"\x00" * 32), b"\x02\x01\x00")

    def test_sequence_wraps_both_integers(self):
        der = pv.raw_sig_to_der(b"\x01" * 32 + b"\x02" * 32)
        self.assertEqual(der[0], 0x30)
        self.assertEqual(der[1], len(der) - 2)

    def test_wrong_signature_length_rejected(self):
        with self.assertRaises(ValueError):
            pv.raw_sig_to_der(b"\x00" * 63)


class PemTests(unittest.TestCase):
    def test_rejects_wrong_length(self):
        with self.assertRaises(ValueError):
            pv.point_to_pem(b"\x04" + b"\x00" * 63)

    def test_rejects_compressed_point(self):
        with self.assertRaises(ValueError):
            pv.point_to_pem(b"\x02" + b"\x00" * 64)

    @needs_openssl
    def test_openssl_accepts_the_encoding(self):
        # The hand-rolled SPKI prefix is only correct if openssl parses it.
        pem = pv.point_to_pem(KAT_POINT)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "pub.pem")
            with open(p, "wb") as fh:
                fh.write(pem)
            r = subprocess.run(["openssl", "pkey", "-pubin", "-in", p, "-noout", "-text"],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("prime256v1", r.stdout)


class FramingTests(unittest.TestCase):
    def test_short_frame(self):
        self.assertEqual(pv.check_framing(b"\xa1\x50\x02"), pv.E_MALFORMED)

    def test_bad_magic(self):
        f = bytearray(build_prefix())
        f[0] = 0xA2
        self.assertEqual(pv.check_framing(bytes(f) + b"\x00" * 64), pv.E_MALFORMED)

    def test_bad_version(self):
        f = build_prefix(version=1) + b"\x00" * 64
        self.assertEqual(pv.check_framing(f), pv.E_MALFORMED)

    def test_v2_frame_is_refused_rather_than_reinterpreted(self):
        # v2 carried no range-integrity evidence. Read under v3 its uptime field
        # would supply the flags byte, so a stale frame could appear to claim a
        # good STS it never made. The version check has to catch it first.
        f = build_prefix(version=2) + b"\x00" * 64
        self.assertEqual(pv.check_framing(f), pv.E_MALFORMED)

    def test_retired_hmac_frame_reports_wrong_algorithm_not_malformed(self):
        # A well-formed frame naming the retired v1 HMAC algorithm is a stale
        # peer, not corruption -- and its length is wrong for this alg too.
        f = build_prefix(alg=1) + b"\x00" * 32
        self.assertEqual(pv.check_framing(f), pv.E_ALG)

    def test_algorithm_is_checked_before_length(self):
        # Same wrong algorithm, wrong length too: still E_ALG, matching the C.
        f = build_prefix(alg=1) + b"\x00" * 64
        self.assertEqual(pv.check_framing(f), pv.E_ALG)

    def test_wrong_length_for_the_named_algorithm(self):
        f = build_prefix() + b"\x00" * 63
        self.assertEqual(pv.check_framing(f), pv.E_MALFORMED)

    def test_good_framing(self):
        self.assertEqual(pv.check_framing(build_prefix() + b"\x00" * 64), pv.OK)


class ParseTests(unittest.TestCase):
    def test_fields_decode_big_endian(self):
        f = pv.parse_fields(KAT_FRAME)
        self.assertEqual(f["alg"], pv.ALG_ECDSA_P256)
        self.assertEqual(f["status"], pv.PRESENCE_PRESENT)
        self.assertEqual(f["nonce"], KAT_NONCE)
        self.assertEqual(f["cred_id"], KAT_CREDID)
        self.assertEqual(f["distance_cm"], 25)
        self.assertEqual(f["range_flags"], pv.RANGE_STS_OK)
        self.assertEqual(f["sts_quality"], 300)
        self.assertEqual(f["trust_level"], 3)
        self.assertEqual(f["uptime_ms"], 1000000)
        self.assertEqual(f["unix_ms"], 1785000000000)

    def test_sts_quality_decodes_signed(self):
        # An unsigned read would turn a negative index into ~65000 and sail past
        # any floor a verifier set.
        prefix = build_prefix(sts_quality=-1234)
        f = pv.parse_fields(prefix + b"\x00" * 64)
        self.assertEqual(f["sts_quality"], -1234)


@needs_openssl
class KatTests(unittest.TestCase):
    """The fixed vector: a real signature over known bytes must verify."""

    def test_kat_verifies(self):
        verdict, f = pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.OK, pv.VERDICT_REASON[verdict])
        self.assertEqual(f["distance_cm"], 25)

    def test_wrong_nonce_rejected(self):
        verdict, _ = pv.verify(
            KAT_FRAME,
            KAT_POINT,
            b"\x00" * 16,
            expected_cred_id=KAT_CREDID,
            max_cm=40,
        )
        self.assertEqual(verdict, pv.E_NONCE)

    def test_wrong_credential_rejected(self):
        verdict, _ = pv.verify(
            KAT_FRAME,
            KAT_POINT,
            KAT_NONCE,
            expected_cred_id=b"\x00" * pv.CREDID_LEN,
            max_cm=40,
        )
        self.assertEqual(verdict, pv.E_CREDENTIAL)

    def test_distance_over_threshold_rejected(self):
        verdict, _ = pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=24)
        self.assertEqual(verdict, pv.E_RANGE)

    def test_threshold_is_inclusive(self):
        verdict, _ = pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=25)
        self.assertEqual(verdict, pv.OK)

    def test_uptime_not_moving_forward_rejected(self):
        verdict, _ = pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40,
                               min_uptime_ms=1000000)
        self.assertEqual(verdict, pv.E_STALE)

    def test_uptime_moving_forward_accepted(self):
        verdict, _ = pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40,
                               min_uptime_ms=999999)
        self.assertEqual(verdict, pv.OK)

    def test_any_tampered_signed_byte_is_rejected(self):
        # Flipping a bit anywhere in the signed prefix must break the signature.
        for off in (pv.OFF_STATUS, pv.OFF_NONCE, pv.OFF_CREDID,
                    pv.OFF_DISTANCE, pv.OFF_RANGE_FLAGS, pv.OFF_STS_QUALITY,
                    pv.OFF_TRUST, pv.OFF_UPTIME, pv.OFF_UNIX):
            with self.subTest(offset=off):
                t = bytearray(KAT_FRAME)
                t[off] ^= 0x01
                verdict, _ = pv.verify(
                    bytes(t), KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40
                )
                self.assertEqual(verdict, pv.E_MAC)

    def test_tampered_signature_is_rejected(self):
        t = bytearray(KAT_FRAME)
        t[pv.OFF_TAG] ^= 0x01
        verdict, _ = pv.verify(bytes(t), KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_MAC)

    def test_wrong_public_key_is_rejected(self):
        other, _ = gen_key_and_sign(b"unrelated")
        verdict, _ = pv.verify(KAT_FRAME, other, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_MAC)


@needs_openssl
class IntegrityTests(unittest.TestCase):
    """Frames whose distance is inside the threshold but was never vouched for.

    Each is signed for real, so the only thing standing between the frame and
    acceptance is the evidence check. That is the point: a distance-reduction
    attack does not have to produce a malformed frame or a bad signature. It
    produces a perfectly well-formed one carrying a number it chose.
    """

    def signed(self, **kw):
        prefix = build_prefix(**kw)
        point, sig = gen_key_and_sign(prefix)
        return prefix + sig, point

    def test_sts_not_ok_is_rejected(self):
        frame, point = self.signed(range_flags=0)
        verdict, f = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_INTEGRITY)
        # Fields still come back for logging: the operator needs to see WHICH
        # distance was refused, not just that something was.
        self.assertEqual(f["distance_cm"], 25)

    def test_unknown_flag_bit_is_rejected(self):
        frame, point = self.signed(range_flags=pv.RANGE_STS_OK | 0x80)
        verdict, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_INTEGRITY)

    def test_distance_is_checked_before_integrity(self):
        # Mirrors the C's order, so the two verifiers name the same reason for
        # the same frame.
        frame, point = self.signed(distance=41, range_flags=0)
        verdict, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_RANGE)

    def test_quality_floor_can_be_tightened_above_the_producers(self):
        frame, point = self.signed(sts_quality=50)
        ok, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(ok, pv.OK)
        tight, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40,
                             min_sts_quality=51)
        self.assertEqual(tight, pv.E_INTEGRITY)

    def test_default_quality_floor_changes_nothing(self):
        # 0 must be inert: the producer's own floor already refuses a negative
        # index, so the default must not add policy nobody asked for.
        frame, point = self.signed(sts_quality=0)
        verdict, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.OK)

    def test_a_good_frame_still_passes(self):
        # Negative control for the whole class: if this failed, the rejections
        # above would prove nothing about the evidence check.
        frame, point = self.signed()
        verdict, _ = pv.verify(frame, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.OK)

    def test_all_zero_public_key_never_verifies(self):
        # What the dongle reports when it has no signing key.
        verdict, _ = pv.verify(
            KAT_FRAME, b"\x00" * 65, KAT_NONCE, KAT_CREDID, max_cm=40
        )
        self.assertEqual(verdict, pv.E_MAC)

    def test_no_fields_returned_when_the_signature_fails(self):
        # A forger must not be able to put values into a caller's log.
        verdict, f = pv.verify(
            KAT_FRAME, b"\x00" * 65, KAT_NONCE, KAT_CREDID, max_cm=40
        )
        self.assertEqual(verdict, pv.E_MAC)
        self.assertIsNone(f)


@needs_openssl
class RoundTripTests(unittest.TestCase):
    """Fresh keys each run, so the KAT above cannot be passing by luck."""

    def test_fresh_key_round_trip(self):
        prefix = build_prefix(distance=12, uptime=4242)
        point, sig = gen_key_and_sign(prefix)
        verdict, f = pv.verify(prefix + sig, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.OK, pv.VERDICT_REASON[verdict])
        self.assertEqual(f["distance_cm"], 12)
        self.assertEqual(f["uptime_ms"], 4242)

    def test_absent_status_rejected_after_a_valid_signature(self):
        prefix = build_prefix(status=pv.PRESENCE_ABSENT, distance=pv.DIST_NONE)
        point, sig = gen_key_and_sign(prefix)
        verdict, f = pv.verify(prefix + sig, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_ABSENT)
        self.assertEqual(f["distance_cm"], pv.DIST_NONE)

    def test_dist_none_rejected_even_when_present(self):
        # A dongle claiming PRESENT with no range must not pass a threshold test.
        prefix = build_prefix(status=pv.PRESENCE_PRESENT, distance=pv.DIST_NONE)
        point, sig = gen_key_and_sign(prefix)
        verdict, _ = pv.verify(
            prefix + sig, point, KAT_NONCE, KAT_CREDID, max_cm=0xFFFF
        )
        self.assertEqual(verdict, pv.E_RANGE)

    def test_signature_from_a_different_prefix_does_not_transfer(self):
        prefix = build_prefix(distance=12)
        point, sig = gen_key_and_sign(prefix)
        other = build_prefix(distance=11)
        verdict, _ = pv.verify(other + sig, point, KAT_NONCE, KAT_CREDID, max_cm=40)
        self.assertEqual(verdict, pv.E_MAC)


class OpensslMissingTests(unittest.TestCase):
    def test_missing_binary_raises_rather_than_reporting_a_bad_signature(self):
        # "cannot check" must never be reported as "check failed".
        with self.assertRaises(pv.OpensslMissing):
            pv.verify(KAT_FRAME, KAT_POINT, KAT_NONCE, KAT_CREDID, max_cm=40,
                      openssl="/nonexistent/openssl-does-not-exist")

    def test_cli_exits_2_when_openssl_is_missing(self):
        with quiet():
            rc = pv.main(["--pubkey", KAT_POINT.hex(), "--frame", KAT_FRAME.hex(),
                          "--nonce", KAT_NONCE.hex(),
                          "--cred-id", KAT_CREDID.hex(),
                          "--openssl", "/nonexistent/openssl-does-not-exist"])
        self.assertEqual(rc, 2)


class CliTests(unittest.TestCase):
    def test_bad_nonce_length_is_refused(self):
        with self.assertRaises(SystemExit):
            pv.main(["--pubkey", KAT_POINT.hex(), "--frame", KAT_FRAME.hex(),
                     "--nonce", "aabb"])

    def test_non_hex_input_is_refused(self):
        with self.assertRaises(SystemExit):
            pv.main(["--pubkey", "nothexatall", "--frame", KAT_FRAME.hex(),
                     "--nonce", KAT_NONCE.hex()])

    def test_reads_hex_from_a_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "frame.hex")
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(KAT_FRAME.hex() + "\n")
            self.assertEqual(pv.read_bytes(p, "--frame"), KAT_FRAME)

    def test_reads_raw_bytes_from_a_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "frame.bin")
            with open(p, "wb") as fh:
                fh.write(KAT_FRAME)
            self.assertEqual(pv.read_bytes(p, "--frame"), KAT_FRAME)

    @needs_openssl
    def test_exit_zero_only_on_presence(self):
        with quiet():
            ok = pv.main(["--pubkey", KAT_POINT.hex(), "--frame", KAT_FRAME.hex(),
                          "--nonce", KAT_NONCE.hex(), "--cred-id", KAT_CREDID.hex(),
                          "--max-cm", "40"])
            bad = pv.main(["--pubkey", KAT_POINT.hex(), "--frame", KAT_FRAME.hex(),
                           "--nonce", KAT_NONCE.hex(), "--cred-id", KAT_CREDID.hex(),
                           "--max-cm", "10"])
        self.assertEqual(ok, 0)
        self.assertEqual(bad, 1)

    @needs_openssl
    def test_json_output_carries_the_verdict(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            pv.main(["--pubkey", KAT_POINT.hex(), "--frame", KAT_FRAME.hex(),
                     "--nonce", KAT_NONCE.hex(), "--cred-id", KAT_CREDID.hex(),
                     "--json"])
        out = json.loads(buf.getvalue())
        self.assertEqual(out["verdict"], "OK")
        self.assertEqual(out["fields"]["distance_cm"], 25)
        self.assertEqual(out["fields"]["cred_id"], KAT_CREDID.hex())


if __name__ == "__main__":
    if not HAVE_OPENSSL:
        print("  presence-verify: openssl not found, curve tests will skip")
    unittest.main(verbosity=1)
