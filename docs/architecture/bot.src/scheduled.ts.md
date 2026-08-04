<!-- generated documentation — edit the source, not this file -->
# `bot/src/scheduled.ts`

@file The escalation sweep: "asleep candidates get pinged on a follow-up
if nobody accepts within a configurable window." Runs off a Cron Trigger
(wrangler.toml `[triggers]`) rather than any in-request timer, since a
Worker has no way to schedule work minutes after a request has already
finished — this is the one part of the bot that is not driven by a
Discord interaction at all.

**depends on** [`bot/src/discordRest.ts`](discordRest.ts.md), [`bot/src/oauthLinks.ts`](oauthLinks.ts.md), [`bot/src/oauthState.ts`](oauthState.ts.md), [`bot/src/testRequestContainer.ts`](testRequestContainer.ts.md), [`bot/src/testRequests.ts`](testRequests.ts.md)  ·  **used by** [`bot/src/index.ts`](index.ts.md)

## API

### `export async function runEscalationSweep(db: D1Database | undefined, botToken: string | undefined, correlationId: string, now: number, escalateMinutes: number): Promise<SweepResult>`
`bot/src/scheduled.ts:37`

One sweep tick. Every step degrades independently: a request whose
candidate lookup or escalation-mark fails is skipped (logged) rather
than aborting the whole sweep, so one bad row cannot block every other
pending request's escalation.

**called by** `scheduled`  ·  **calls** `asleepUnpingedCandidates`, `buildEscalationPing`, `markEscalated`, `pendingUnescalated`, `postMessage`

### `export async function runAbandonedLinkPurge(db: D1Database | undefined, correlationId: string, now: number): Promise<number>`
`bot/src/scheduled.ts:101`

Delete OAuth rows stranded between the two legs.
Separate from the escalation sweep and not folded into it, because the two
have different dependencies: escalation needs a bot token and does nothing
without one, while this needs only D1. A deployment that never set
DISCORD_BOT_TOKEN must still not accumulate live credentials.
The cutoff is the OAuth state TTL. Past it the state row is refused at read
time, so the flow that would have scrubbed these tokens can no longer be
completed by anyone — the row is unreachable, not merely slow.

**called by** `scheduled`  ·  **calls** `purgeAbandonedLinks`

<details><summary>Undocumented (1)</summary>

- `escalateMinutesFrom` — tested: :escalate minutes from falls back to the documented default when unset or garbage@l32

</details>
