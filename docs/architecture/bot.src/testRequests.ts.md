<!-- generated documentation — edit the source, not this file -->
# `bot/src/testRequests.ts`

@file Every statement this Worker runs against `test_requests` and
`test_request_candidates`. Same rule as rigs.ts: no SQL built by
concatenation, every value a bound parameter.

**used by** [`bot/src/commands/test-request.ts`](../bot.src.commands/test-request.ts.md), [`bot/src/commands/test-result.ts`](../bot.src.commands/test-result.ts.md), [`bot/src/scheduled.ts`](scheduled.ts.md)

## API

### `export class RoutingUnavailable extends Error`
`bot/src/testRequests.ts:31`

Thrown when the D1 binding is missing or a statement fails. A distinct
class from rigs.ts's RegistryUnavailable: this is the routing state, not
the hardware registry, and the two should be able to fail independently
in a user-facing message without conflating them.

### `export async function createTestRequest(db: D1Database | undefined, req: NewTestRequest): Promise<void>`
`bot/src/testRequests.ts:80`

Creates the request row and every candidate row in one D1 batch, so a
request never exists with a partial candidate list. `pinged_awake` is
seeded from `awake` itself: an awake candidate is pinged the moment the
request is posted, so there is nothing left to mark after the fact.

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function claim(db: D1Database | undefined, requestId: string, userId: string): Promise<boolean>`
`bot/src/testRequests.ts:104`

Atomic first-accept-wins: a single UPDATE with the guard in its own WHERE
clause rather than a read-then-write, so two simultaneous Accept clicks
cannot both succeed (same pattern as cooldown.ts's checkMatrixCooldown).
Returns whether this call was the one that claimed it.

**called by** `componentHandler`  ·  **calls** `need`, `run`

### `export async function getRequestByThreadId(db: D1Database | undefined, threadId: string): Promise<TestRequestRow | null>`
`bot/src/testRequests.ts:118`

`/test-result` runs inside the claim thread, not the queue channel, so it
has to work backwards from "which request does this thread belong to".

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function markDone(db: D1Database | undefined, requestId: string): Promise<boolean>`
`bot/src/testRequests.ts:128`

Atomic claimed->done guard, same first-writer-wins shape as `claim()` —
a double /test-result submission (a retry, a duplicate interaction
delivery) writes at most one validation row per call site that checks
this return value.

**called by** `handler`  ·  **calls** `need`, `run`

### `export async function pendingUnescalated(db: D1Database | undefined, cutoffMs: number): Promise<TestRequestRow[]>`
`bot/src/testRequests.ts:153`

Requests that have sat pending, unescalated, since before `cutoffMs` —
the scheduled sweep's candidate set for waking the asleep half of the
candidate list.

**called by** `runEscalationSweep`  ·  **calls** `need`, `run`

### `export async function markEscalated(db: D1Database | undefined, requestId: string, now: number, pingedUserIds: readonly string[]): Promise<void>`
`bot/src/testRequests.ts:180`

Marks the request escalated and every named candidate as pinged, in one
batch — the sweep must not re-ping the same asleep candidates on its next
tick just because one write succeeded and the other did not.

**called by** `runEscalationSweep`  ·  **calls** `need`, `run`

<details><summary>Undocumented (6)</summary>

- `RoutingUnavailable.constructor`
- `need`
- `run`
- `setThread` — tested: :get request by thread id finds a request by its thread once claimed, and finds nothing before that@l132; :set thread fills in the thread id once someone accepts, the one field genuinely unknown at creation@l50
- `getRequest` — tested: :claim is first-accept-wins: the second call on an already-claimed request returns false and changes nothing@l62; :create test request writes the request (with the message id already known) and every candidate atomically@l34; :mark done is an atomic claimed->done guard: only fires from 'claimed', and only once@l146; :mark escalated marks the request and only the named candidates as pinged, so a partial ping list doesn't skip anyone next sweep@l103; :set thread fills in the thread id once someone accepts, the one field genuinely unknown at creation@l50
- `asleepUnpingedCandidates` — tested: :mark escalated marks the request and only the named candidates as pinged, so a partial ping list doesn't skip anyone next sweep@l103

</details>
