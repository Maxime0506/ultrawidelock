<!-- generated documentation — edit the source, not this file -->
# `tools/aliro_blob.py`

Inspect an aliro_prov ("APRV") reader-provisioning blob.

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

**discussed in** [`firmware/README.md`](../../../firmware/README.md)

## API

### `class BadBlob(Exception)`
`tools/aliro_blob.py:52`

Exception raised when a blob structure is invalid or cannot be parsed.

**called by** `parse`

### `parse(buf, off=0)`
`tools/aliro_blob.py:57`

Parse one blob at buf[off:]. Returns (fields, total_len).

**called by** `main`  ·  **calls** `BadBlob`

### `check(f)`
`tools/aliro_blob.py:133`

Return the list of reasons this blob will not produce a Wallet unlock.

**called by** `report`

### `report(f, total, args, where='')`
`tools/aliro_blob.py:156`

Print a structured report of an APRV blob to stdout showing version, identity, reader ID, signing key, GRK, trust anchors, and unlock verdict.

**called by** `main`  ·  **calls** `check`

<details><summary>Undocumented (1)</summary>

- `main`

</details>
