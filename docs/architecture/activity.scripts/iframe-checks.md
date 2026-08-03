<!-- generated documentation — edit the source, not this file -->
# `activity/scripts/iframe-checks.py`

Two Activity checklist items that only mean anything inside an iframe.

1. single-step mode advances the real RX state machine leg by leg
2. the theme toggle still works when localStorage is unavailable

Both matter because a Discord Activity is a sandboxed iframe: storage can be
refused there, and single-step is the mode people actually use to explain a
DS-TWR round to someone else.

Both are driven in a same-origin iframe so the twin's own DOM can be read back.
Storage is disabled through a Firefox profile pref rather than by faking it, so
the page hits the real exception path its try/catch was written for.

Usage: iframe-checks.py [dist-dir]   (default: ../dist)

**discussed in** [`docs/discord-activity-phase0.md`](../../discord-activity-phase0.md)

<details><summary>Undocumented (4)</summary>

- `H`
- `H.log_message`
- `H.do_POST`
- `H.do_GET`

</details>
