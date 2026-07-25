<!-- generated documentation — edit the source, not this file -->
# `modules/woz_uwb/src/aliro/aliro_device_uwb.c`

Device-side UWB ranging-service setup codec: parses the reader's M1 and M3
setup messages, picks the device's answer to M1 (select_m2), and builds the M2
and M4 replies. The inverse of the reader path in aliro_uwb_msg.c, written over
the same TLV parser and builder helpers. No crypto and no session state, so a
host loopback can drive the real reader codec end to end.

**depends on** [`modules/woz_uwb/src/aliro/aliro_device_uwb.h`](aliro_device_uwb.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg.h`](aliro_uwb_msg.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_builder.h`](aliro_uwb_msg_builder.h.md), [`modules/woz_uwb/src/aliro/aliro_uwb_msg_parser.h`](aliro_uwb_msg_parser.h.md)

## API

### `int aliro_dev_uwb_parse_m1(const uint8_t *msg, size_t len, struct aliro_dev_uwb_m1 *out)`
`modules/woz_uwb/src/aliro/aliro_device_uwb.c:20`

Parse an inbound M1 / M3 message ([proto][id][len_be16][attrs]) into the
structs above. Returns 0 on success, -1 on a header/attribute mismatch.

### `void aliro_dev_uwb_select_m2(const struct aliro_dev_uwb_m1 *m1, struct aliro_dev_uwb_m2_params *out)`
`modules/woz_uwb/src/aliro/aliro_device_uwb.c:134`

Pick M2 selections from a parsed M1: echo the reader's first config, pulse
shape and channel, and default the remaining fields to values the reader
accepts. A real device would consult its own capabilities here.

### `struct aliro_uwb_message *aliro_dev_uwb_build_m2(const struct aliro_dev_uwb_m2_params *p)`
`modules/woz_uwb/src/aliro/aliro_device_uwb.c:146`

Build an M2 / M4 message. Returns a heap-allocated message (free with
aliro_uwb_msg_free), or NULL on allocation/encode failure.

<details><summary>Undocumented (2)</summary>

- `aliro_dev_uwb_parse_m3` — tested: aliro device uwb
- `aliro_dev_uwb_build_m4` — tested: aliro device uwb

</details>
