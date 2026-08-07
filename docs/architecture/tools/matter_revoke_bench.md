<!-- generated documentation — edit the source, not this file -->
# `tools/matter_revoke_bench.py`

Drive ClearCredential and ClearUser at a running lock, over a PASE session.

Revocation is the one part of the Matter surface no walk-up can exercise: the
commands only ever arrive from an admin, and the only admin this lock has is
Apple Home, which sends them when it feels like it. This sends them on demand.

  python3 tools/matter_revoke_bench.py --dry-run          encode only, no board
  python3 tools/matter_revoke_bench.py 1234-567-8901      both proofs
  python3 tools/matter_revoke_bench.py <code> --only A    ClearCredential only
  python3 tools/matter_revoke_bench.py <code> --only B    ClearUser only

The code is what Apple Home shows under the accessory's "Turn On Pairing Mode"
(11 digits; dashes optional). Options: --endpoint (default 1), --only, --index
and --user pick the slots to use, --storage points the controller's key store
somewhere other than beside this file.

IT NEVER COMMISSIONS. A commissioning window is a PASE responder, so this opens
a PASE session and invokes over that -- no AddNOC, no second fabric consumed,
nothing to undo, and the window closes on its own timeout. That matters on a
board with two fabric slots and one already spent on Apple.

Needs the CHIP controller stack, which ships as wheels and needs no SDK build:

  python3 -m venv /tmp/mctl
  /tmp/mctl/bin/pip install home-assistant-chip-clusters home-assistant-chip-core
  /tmp/mctl/bin/python tools/matter_revoke_bench.py --dry-run

Run `make monitor` alongside: the lock logs "ALIRO CREDENTIAL ADDED" on the
install and "ALIRO CLEAR ... REVOKED" on the removal, and those lines are the
actual evidence. This script only proves the commands were accepted.

**used by** [`tools/matter_cap_probe.py`](matter_cap_probe.md)  ·  **discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `_load()`
`tools/matter_revoke_bench.py:70`

Import the CHIP controller stack, or explain what to install.

**called by** `main`

### `parse_code(code)`
`tools/matter_revoke_bench.py:81`

Split a manual pairing code into (passcode, discriminator).

**called by** `run`

### `class Bench`
`tools/matter_revoke_bench.py:95`

The two proofs, against either a real controller or the dry-run stand-in.

**called by** `run`

#### `Bench.cmd(self, what, payload)`
`tools/matter_revoke_bench.py:103`

One timed invoke. A refusal is recorded, never raised: the later steps
still say something useful about a lock that rejected an earlier one.

**called by** `Bench.install`, `Bench.proof_a`, `Bench.proof_b`  ·  **calls** `DryCtrl.SendCommand`

#### `Bench.install(self, user_index, cred_index, key=TEST_KEY)`
`tools/matter_revoke_bench.py:139`

SetUser then SetCredential, which is the order Apple uses: this node
wants an explicit user index and never allocates one itself.

**called by** `Bench.proof_a`, `Bench.proof_b`  ·  **calls** `Bench.check`, `Bench.cmd`, `Bench.credential`

### `class DryCtrl`
`tools/matter_revoke_bench.py:181`

Encodes every payload the bench would send, with no board and no radio.

Worth having as more than a smoke test: the bytes it prints are the ones
tests/host/test_matter_clusters.c replays through the firmware's decoder, so
a field renumbered upstream shows up here before it shows up on a bench.

**called by** `run`

<details><summary>Undocumented (11)</summary>

- `Bench.__init__`
- `Bench.check`
- `Bench.credential`
- `Bench.caps`
- `Bench.proof_a`
- `Bench.proof_b`
- `DryCtrl.SendCommand`
- `DryCtrl.ReadAttribute`
- `verdict`
- `run`
- `main`

</details>
