<!-- generated documentation — edit the source, not this file -->
# `modules/woz_anchor/src/woz_report.c`

*No module docstring. First commit: "woz_anchor: ARP1 range-report line codec and its host consumer".*

**depends on** [`modules/woz_anchor/include/woz_report.h`](../modules.woz_anchor.include/woz_report.h.md)

## API

### `static size_t put_u64(char *out, size_t cap, uint64_t v)`
`modules/woz_anchor/src/woz_report.c:42`

Append an unsigned 64-bit value in decimal. Returns bytes written, or 0 if it
would not fit; the caller checks once at the end rather than at every field,
because a partial line is never emitted.

**called by** `app_u64`, `put_i32`

### `struct app`
`modules/woz_anchor/src/woz_report.c:119`

Every append goes through this so a single overflow check at the end is
enough: once `fail` is set nothing more is written.

### `static uint64_t scan_u64(struct scan *s, uint64_t limit)`
`modules/woz_anchor/src/woz_report.c:251`

Reads one decimal field. Rejects an empty field and anything that would
exceed `limit`, so a corrupted line cannot wrap a counter into a plausible
small number.

**called by** `scan_i32`, `woz_report_parse`  ·  **calls** `skip_space`

<details><summary>Undocumented (13)</summary>

- `woz_report_crc16`
- `put_i32`
- `put_char`
- `put_hex4`
- `app_u64`
- `app_i32`
- `app_ch`
- `app_str`
- `woz_report_format`
- `scan`
- `skip_space`
- `scan_i32`
- `woz_report_parse`

</details>
