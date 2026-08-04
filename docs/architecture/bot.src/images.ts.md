<!-- generated documentation — edit the source, not this file -->
# `bot/src/images.ts`

@file The images somebody can be running.
The make targets and their build directories, from the dispatcher in
`Makefile` and the per-target recipes in `mk/`. Bare targets mean the
DWM3001CDK; the nRF5340 DK is `nrf-` prefixed and the ESP32 is `esp-`
prefixed.
"Not sure" is a real option on purpose. Somebody who does not know which
image they flashed is exactly the person filing the report, and forcing a
guess would put a wrong answer into the context block rather than a blank.

**used by** [`bot/src/commands/help-me.ts`](../bot.src.commands/help-me.ts.md)

<details><summary>Undocumented (2)</summary>

- `isKnownImage`
- `imageLabel`

</details>
