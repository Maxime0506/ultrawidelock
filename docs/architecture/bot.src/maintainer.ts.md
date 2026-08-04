<!-- generated documentation — edit the source, not this file -->
# `bot/src/maintainer.ts`

@file The maintainer allow-list, shared by every maintainer-only command
(`/who-has`, `/test-request`). A `default_member_permissions: "0"` on the
command definition hides it from non-administrators, but that is a guild
setting anyone with server admin can change; this check is not.

**used by** [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/who-has.ts`](../bot.src.commands/who-has.ts.md)

## API

### `export function isMaintainer(env: { MAINTAINER_IDS?: string }, userId: string): boolean`
`bot/src/maintainer.ts:10`

Exact match against the configured list. Empty list means nobody, which
fails closed: an unset binding must not open the command up.

**called by** `handler`, `handler`
