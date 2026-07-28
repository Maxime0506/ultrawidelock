<!-- generated documentation — edit the source, not this file -->
# `tools/presence_git.py`

Presence-signed git tags: prove a human was physically present at a release.

A GPG signature proves WHO made a tag. It does not prove they were there: a
stolen key, a compromised CI runner or a coerced automated pipeline all produce
perfectly valid signatures. This adds the orthogonal claim -- that a provisioned
credential was physically within a few tens of centimetres of the machine when
the tag was made -- measured by UWB time-of-flight. The protocol structure
resists a simple relay shortening distance, but this project's relay resistance
has not been experimentally measured.

    presence_git.py enroll --port /dev/tty.usbmodem... --name my-dongle
    presence_git.py sign   --tag presence/1.2.0 --port /dev/tty.usbmodem...
    presence_git.py verify --tag presence/1.2.0          # what CI runs

HOW THE NONCE WORKS, AND WHAT THAT COSTS
Everywhere else in this protocol the verifier mints a random nonce and remembers
it, which makes replay impossible. CI cannot do that: it was not present when
the tag was made, so it has no nonce to remember. The nonce is therefore DERIVED
from what is being signed:

    nonce = SHA-256("openaliro-presence-tag-v1\0" + tag + "\0" + commit)[:16]

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

Enrolled keys and allowed credential ids live in .presence/enrolled, committed
to the repo so both the trusted dongle and named human are reviewable in history
like any other change. The tag carries only a key id; policy always comes from
that file, because a tag that carried its own keys would authorize itself.

Stdlib only, except that enroll/sign import pyserial lazily to talk to a real
dongle. verify -- the half CI runs -- needs no serial port and no extra package.

**depends on** [`tools/presence_verify.py`](presence_verify.md)  ·  **used by** [`host/presence/presence_service.py`](../host.presence/presence_service.md)  ·  **discussed in** [`docs/presence.md`](../../presence.md)

## API

### `key_id(point: bytes) -> bytes`
`tools/presence_git.py:114`

Stable 8-byte id for a public key: first 8 bytes of SHA-256(point).

Same construction as aliro_assert_cred_id() uses for credentials, so the two
identifier schemes in this system read alike.

**called by** `cmd_enroll`, `cmd_probe`, `cmd_sign`, `read_enrolled`

### `binding_nonce(tag: str, commit: str) -> bytes`
`tools/presence_git.py:123`

The 16-byte challenge nonce a tag's assertion must echo.

**called by** `cmd_nonce`, `cmd_sign`, `verify_tag`

### `tag_commit(tag: str, cwd=None) -> str`
`tools/presence_git.py:141`

The commit a tag resolves to, dereferencing annotated tags.

**called by** `cmd_nonce`, `verify_tag`  ·  **calls** `git`

### `tag_trailer(tag: str, key: str, cwd=None) -> str`
`tools/presence_git.py:146`

One trailer value from a tag message, or "" if absent.

Uses git's own trailer parser rather than scanning the message, because an
annotated tag may also carry a PGP signature block and hand-rolled scanning
would have to know to step around it.

**called by** `verify_tag`  ·  **calls** `git`

### `read_enrolled(path=ENROLLED_PATH, root=None) -> dict`
`tools/presence_git.py:156`

Load trusted dongles. Returns {key_id_hex: (name, point, cred_id)}.

**called by** `cmd_enroll`, `cmd_sign`, `verify_tag`  ·  **calls** `PresenceError`, `key_id`

### `verify_tag(tag: str, max_cm=40, root=None, enrolled_path=ENROLLED_PATH, openssl='openssl')`
`tools/presence_git.py:191`

Verify a tag's presence assertion. Returns (verdict, detail-dict).

A verdict of None means the tag carries no assertion at all, which is not a
failure by itself -- callers decide whether an unsigned tag is acceptable.

**called by** `cmd_verify`  ·  **calls** `PresenceError`, `binding_nonce`, `read_enrolled`, `tag_commit`, `tag_trailer`

### `wait_ready(ser) -> bool`
`tools/presence_git.py:241`

Poll a newline until the board's shell prompts back. True if it did.

Returning rather than raising on the deadline keeps the diagnosis with the
caller: a port that never prompts is usually the wrong port or firmware with
no shell at all, and ask() already names both.

**called by** `open_port`

### `read_line(ser) -> str`
`tools/presence_git.py:299`

Read one line, or raise on a silent port. Undecodable bytes are not fatal.

Console output is not guaranteed to be clean UTF-8 -- a board reset mid-read
puts ROM garbage on the wire -- and a decode error there should look like a
line that does not match, not like a crash.

**called by** `ask`, `import_identity`, `read_bare_hex`  ·  **calls** `PresenceError`

### `ask(ser, command: str, tag: str, what: str) -> str`
`tools/presence_git.py:312`

Send a console command and return the payload of the first line tagged `tag`.

Answers are located by tag rather than by position because the dongle's console
also carries the log stream and the shell's own echo. That is the whole reason
this protocol is lines of text: an interleaved log line is a line that does not
match, where in a binary framing it was a corrupted response.

**called by** `dongle_credential`, `dongle_prove`, `dongle_pubkey`  ·  **calls** `PresenceError`, `read_line`

### `export_identity(ser) -> str`
`tools/presence_git.py:393`

Read a reader identity + trust blob from a provisioned board, as hex.

**called by** `cmd_clone`  ·  **calls** `read_bare_hex`

### `import_identity(ser, blob_hex: str) -> str`
`tools/presence_git.py:407`

Load an identity blob into a board. Returns the console's confirmation line.

**called by** `cmd_clone`  ·  **calls** `PresenceError`, `read_line`

### `cmd_probe(args) -> int`
`tools/presence_git.py:472`

Run one complete fresh transaction, range, signature and verification.

**calls** `dongle_credential`, `dongle_prove`, `dongle_pubkey`, `key_id`, `open_port`

### `cmd_clone(args) -> int`
`tools/presence_git.py:545`

Copy a provisioned reader identity onto the dongle over two serial ports.

This is what lets a phone's EXISTING Wallet credential transact with the dongle:
the credential was issued against a particular reader identity, so the dongle has
to present that same identity rather than be enrolled separately. The blob
carries the reader private key, which is exactly why the console command behind
it is not compiled in by default.

**calls** `export_identity`, `import_identity`, `open_port`

### `validate_tag_name(tag: str) -> None`
`tools/presence_git.py:573`

Reject a tag outside the presence namespace.

Called before the port is opened on purpose. Signing costs a phone wake and a
walk to the reader (an asleep phone refuses every transaction), so a name this
tool was never going to accept must fail while the user is still at the keyboard,
not after they have stood in front of the board.

**called by** `cmd_sign`  ·  **calls** `PresenceError`

<details><summary>Undocumented (14)</summary>

- `PresenceError` — tested: firmware timeout is safe and does not return the nonce
- `git`
- `open_port` — tested: open asserts dtr when the board stays silent; open sets console lines before open and drains boot output
- `unhex`
- `read_bare_hex`
- `dongle_pubkey` — tested: dongle without a key is refused; non hex answer fails loudly; non uncompressed point is refused; pubkey request sends the console command; stale input is flushed before each request; truncated answer fails loudly
- `dongle_credential` — tested: credential request sends the console command
- `dongle_prove` — tested: challenge sends the nonce as hex; dongle error line is surfaced not swallowed; log lines around the answer are ignored; silent port fails with the same advice; stale input is flushed before each request; wrong firmware fails with advice
- `cmd_nonce`
- `cmd_enroll`
- `cmd_sign`
- `cmd_verify`
- `build_parser`
- `main`

</details>
