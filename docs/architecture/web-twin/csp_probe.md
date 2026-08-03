<!-- generated documentation — edit the source, not this file -->
# `web-twin/csp_probe.py`

Phase 0 spike, local half.

Serves web-twin/ with a chosen Content-Security-Policy header and loads it
inside an iframe in headless Firefox, the same shape Discord uses for an
Activity. The wrapper (served with no CSP of its own) reads #selftest out of
the frame and POSTs it back here, so the result lands in stdout rather than in
a screenshot we have to squint at.

Usage: csp_probe.py [dir-to-serve]      (default: the directory holding this file)

**discussed in** [`docs/discord-activity-phase0.md`](../../discord-activity-phase0.md)

<details><summary>Undocumented (5)</summary>

- `find_firefox`
- `Handler`
- `Handler.log_message`
- `Handler.do_POST`
- `Handler.do_GET`

</details>
