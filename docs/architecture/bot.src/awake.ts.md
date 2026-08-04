<!-- generated documentation — edit the source, not this file -->
# `bot/src/awake.ts`

@file Whether a registered owner is awake right now, from their stored
`utc_offset` (minutes) and local awake window (`awake_start`/`awake_end`,
hours 0-23). This is the routing logic the spec calls out by name: "a
router that wakes people at 3 a.m. loses the contributors it was built to
help", so it is built and tested against explicit timezone cases before
anything wires it to a ping.

**used by** [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md)

## API

### `export function isAwakeNow(utcOffsetMinutes: number, awakeStartHour: number, awakeEndHour: number, nowMs: number = Date.now()): boolean`
`bot/src/awake.ts:16`

Each stored hour is treated as a full 60-minute slot, so "8-23" reads as
awake from local 08:00 through local 23:59 inclusive. `boards.ts`
documents equal bounds ("8-8") as "no awake window" for always-on infra;
that convention is honoured here as "always awake" rather than "awake
during one hour of the day", since a single-hour window would make that
documented escape hatch pointless.

**called by** `handler`

### `export function nextWakeUnixMs(utcOffsetMinutes: number, awakeStartHour: number, nowMs: number = Date.now()): number`
`bot/src/awake.ts:41`

The next UTC instant (ms) at which this owner's local clock hits
`awakeStartHour`. Used only to display "next window" on a pending test
request's status card — a display nicety, not routing-critical — so the
exact-boundary edge case (`nowMs` lands precisely on the start minute)
resolves to "tomorrow" rather than "right now" for simplicity.

**called by** `handler`
