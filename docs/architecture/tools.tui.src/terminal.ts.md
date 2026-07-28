<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/terminal.ts`

**used by** [`tools/tui/src/app.tsx`](app.tsx.md)

<details><summary>Undocumented (7)</summary>

- `SerialTerminalBuffer`
- `SerialTerminalBuffer.constructor`
- `SerialTerminalBuffer.write`
- `SerialTerminalBuffer.resize`
- `SerialTerminalBuffer.lines` — tested: :emulates carriage returns, ansi erasure, and partial shell prompts@l4; :retains boot scrollback beyond the visible terminal rows@l22
- `SerialTerminalBuffer.clear`
- `SerialTerminalBuffer.dispose` — tested: :a fade rests on the theme token at both ends and blends only in between@l60; :a pulse starts settled so a frame with no animation clock is still correct@l83; :clears the emulated screen and scrollback@l14; :emulates carriage returns, ansi erasure, and partial shell prompts@l4; :retains boot scrollback beyond the visible terminal rows@l22; :the spinner is silent while nothing is running@l112; :the spinner turns and counts while a job runs, then goes away@l121

</details>
