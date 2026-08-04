<!-- generated documentation — edit the source, not this file -->
# `bot/src/env.ts`

@file Worker bindings.
Every secret here is set with `wrangler secret put NAME`. None of them is
ever committed, logged, or echoed into a response. See bot/README.md for the
full list and how to set it.
One Worker serves both halves of this bot — firmware triage and hardware
compatibility tracking — so this is the union of what both need. Anything
optional degrades the one feature that reads it rather than failing the
Worker, which is what lets `npm test` and a fresh clone run with nothing set.

**used by** [`bot/src/api.ts`](api.ts.md), [`bot/src/command.ts`](command.ts.md), [`bot/src/index.ts`](index.ts.md), [`bot/src/linkedRoles.ts`](linkedRoles.ts.md)
