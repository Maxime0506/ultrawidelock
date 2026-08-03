<!-- generated documentation — edit the source, not this file -->
# `tools/presence_verify.py`

Verify an ECDSA-P256 presence assertion against a dongle's public key.

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

**used by** [`host/presence/presence_service.py`](../host.presence/presence_service.md), [`tools/presence_git.py`](presence_git.md)  ·  **discussed in** [`docs/range-integrity.md`](../../range-integrity.md)

## API

### `class OpensslMissing(RuntimeError)`
`tools/presence_verify.py:133`

Raised when the openssl binary cannot be run.

Deliberately not folded into a verification failure: "we could not check"
and "the signature is bad" are different facts, and silently reporting the
first as the second would turn a broken install into a security verdict.

**called by** `verify_signature`

### `der_uint(v: bytes) -> bytes`
`tools/presence_verify.py:142`

DER-encode a big-endian unsigned integer as an ASN.1 INTEGER.

**called by** `raw_sig_to_der`

### `raw_sig_to_der(sig: bytes) -> bytes`
`tools/presence_verify.py:152`

Convert a raw r||s ECDSA signature to the DER form openssl expects.

**called by** `verify_signature`  ·  **calls** `der_uint`

### `point_to_pem(point: bytes) -> bytes`
`tools/presence_verify.py:162`

Wrap a 65-byte uncompressed P-256 point as a PEM public key.

**called by** `verify_signature`

### `verify_signature(point: bytes, msg: bytes, sig: bytes, openssl: str='openssl') -> bool`
`tools/presence_verify.py:173`

True when sig is a valid ECDSA-P256-SHA256 signature over msg under point.

Returns False for a key that is structurally unusable -- notably the all-zero
point a dongle reports when it has no signing key -- because such a key must
never verify anything.

**called by** `verify`  ·  **calls** `OpensslMissing`, `point_to_pem`, `raw_sig_to_der`

### `parse_fields(wire: bytes) -> dict`
`tools/presence_verify.py:203`

Decode the assertion body. Assumes framing has already been checked.

Parsing before authentication is intentional and matches the C: the fields
are returned for logging even on a reject, and the caller is responsible for
ignoring them unless the verdict is OK.

**called by** `verify`

### `check_framing(wire: bytes, want_alg: int=ALG_ECDSA_P256) -> int`
`tools/presence_verify.py:226`

Length, magic, version and algorithm checks, in the C's exact order.

Algorithm is tested before length on purpose: a well-formed frame for the
other algorithm is a misconfigured peer, not corrupt framing, and the two
have entirely different fixes.

**called by** `verify`

### `verify(wire: bytes, pubkey: bytes, expected_nonce: bytes, expected_cred_id: bytes, max_cm: int=40, min_uptime_ms: int=0, min_sts_quality: int=0, openssl: str='openssl')`
`tools/presence_verify.py:244`

Fully verify a P-256 assertion. Returns (verdict, fields-or-None).

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

**called by** `main`  ·  **calls** `check_framing`, `parse_fields`, `verify_signature`

### `read_bytes(arg: str, name: str) -> bytes`
`tools/presence_verify.py:298`

Accept either a hex string or a path to a file of hex or raw bytes.

**called by** `main`

### `main(argv=None) -> int`
`tools/presence_verify.py:313`

Parse command-line arguments and verify an ECDSA-P256 presence assertion. Arguments include the dongle public key, assertion frame, challenge nonce, credential ID, and optional distance/uptime/STS-quality thresholds. Outputs human-readable or JSON verdict. Returns 0 if the assertion verifies, 1 if it is rejected, 2 if openssl is unavailable.

**calls** `read_bytes`, `verify`
