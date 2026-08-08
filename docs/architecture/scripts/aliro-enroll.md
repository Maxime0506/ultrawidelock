<!-- generated documentation — edit the source, not this file -->
# `scripts/aliro-enroll.py`

Enrol the nRF5340DK initiator into a reader's home, the way Apple Home enrols an iPhone.

Usage:
  # headless, no pairing window, no phone: act as an admin that already
  # commissioned this reader (the HITL path -- see --fabric below: the
  # 2026-08-07 identity in ~/.aliro-chip-tool is chip-tool's DEFAULT
  # fabric, which is alpha, not this script's beta default)
  scripts/aliro-enroll.py --node-id 0x1234 --storage ~/.aliro-chip-tool --fabric alpha

  # first run, BLE route -- the one that works against this reader today
  scripts/aliro-enroll.py --node-id 0x1234 --pairing-code <11 digits>       --dataset <hex from `make monitor`>

  # later runs, already joined, default chip-tool storage
  scripts/aliro-enroll.py --node-id 0x1234

Options:
  --node-id        node id to address the reader by. Required.
  --pairing-code   Apple Home "Turn On Pairing Mode" code. Omit once joined.
  --dataset        active Thread dataset hex. Its presence selects the BLE route
                   instead of IP. Needed because PASE does not run over IP on
                   this reader -- see the comment at step 1. `make monitor`
                   prints it when a window opens.
  --discriminator  LONG 12-bit discriminator. Optional. With it, chip-tool goes
                   straight to BLE and never browses; without it, it browses,
                   finds the IP service first, and burns ~10 s on a PASE that
                   cannot succeed here. Prefer passing it.
  --endpoint       door lock endpoint (default 1)
  --fabric         chip-tool fabric name (default beta, so alpha stays free)
  --out            header to write (default ports/nrf5340dk/initiator/src/bench_identity.h)
  --cred-type      7 evictable / 8 non-evictable endpoint key (default 8)
  --chip-tool      path to the chip-tool binary
  --fresh-storage  run against a private chip-tool KVS, so nothing an earlier
                   attempt persisted can affect this one
  --dry-run        print the chip-tool commands without running them

What it does, in the order it does it:
  1. joins the reader's fabric as a SECOND admin, leaving Apple Home's admin intact
  2. reads the four Aliro attributes that make up the reader's public identity
  3. generates a P-256 credential for the initiator and posts the public half
     with SetCredential, which is what puts it in the reader's trust store
  4. writes a C header the initiator compiles in

The reader is not modified: no firmware change, no shell command, no re-flash. Its
Apple Home fabric, Home tile and walk-up unlock all keep working. Multi-fabric is
ordinary Matter behaviour and the reader treats this admin like any other.

Why a controller is needed at all: a credential enters the trust store only
through Matter SetCredential (firmware/src/matter_commission.c:1966). A phone
never provisions itself -- the home's admin does it on the phone's behalf -- so
standing in for a phone means standing in for the admin too.

The generated header carries the reader's public identity and the initiator's
PRIVATE credential key. It is written outside version control on purpose. Do not
commit it, paste it, or put it in a doc.

## API

### `decode_manual_code(code)`
`scripts/aliro-enroll.py:87`

Return (short_discriminator, passcode) from an 11-digit manual pairing code.

Field layout per the Matter spec section 5.1.3, constants read off
connectedhomeip's src/setup_payload/SetupPayload.h rather than remembered:
chunk1 is 1 digit, chunk2 is 5, chunk3 is 4, then a Verhoeff check digit that
is not needed to decode (chip-tool rejects a bad code on its own).

The passcode comes out exact. The DISCRIMINATOR DOES NOT -- a manual code
carries only its top 4 bits, which is why `pairing ble-thread` needs the long
one supplied separately and why chip-tool browses "_S<4 bits>". Take it from
the board's own SRP log line, which prints D=<full 12 bits>.

**called by** `main`

### `run(cmd, dry_run)`
`scripts/aliro-enroll.py:119`

Run a chip-tool invocation and return its combined output, or die loudly.

**called by** `main`, `read_octets`

### `read_octets(chip_tool, fabric, attr, node_id, endpoint, dry_run, expect_len)`
`scripts/aliro-enroll.py:134`

Read one octet-string attribute and return it as bytes.

chip-tool renders an octet string as a hex blob on the report line. The value
is NULL until Apple Home has provisioned the reader, which the reader reports
deliberately (modules/woz_matter/src/matter_clusters.c:482) -- an unprovisioned
reader has no identity to hand out, and that is a legible failure rather than
a confusing one.

**called by** `main`  ·  **calls** `run`

### `c_array(name, data)`
`scripts/aliro-enroll.py:178`

Render bytes as a C initialiser, 11 per line to match the repo's key tables.

**called by** `main`

<details><summary>Undocumented (1)</summary>

- `main`

</details>
