/**
 * @file Every statement this Worker runs against `test_requests` and
 * `test_request_candidates`. Same rule as rigs.ts: no SQL built by
 * concatenation, every value a bound parameter.
 */

export interface TestRequestRow {
	id: string;
	requester_id: string;
	board: string;
	ios_version: string | null;
	what: string;
	status: string;
	channel_id: string;
	message_id: string;
	claimed_by: string | null;
	thread_id: string | null;
	escalated_at: number | null;
	created_at: number;
}

export interface CandidateInput {
	discordUserId: string;
	awake: boolean;
}

/** Thrown when the D1 binding is missing or a statement fails. A distinct
 *  class from rigs.ts's RegistryUnavailable: this is the routing state, not
 *  the hardware registry, and the two should be able to fail independently
 *  in a user-facing message without conflating them. */
export class RoutingUnavailable extends Error {
	constructor(cause?: unknown) {
		super("routing unavailable");
		this.name = "RoutingUnavailable";
		this.cause = cause;
	}
}

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new RoutingUnavailable();
	return db;
}

async function run<T>(fn: () => Promise<T>): Promise<T> {
	try {
		return await fn();
	} catch (err) {
		throw err instanceof RoutingUnavailable ? err : new RoutingUnavailable(err);
	}
}

export interface NewTestRequest {
	id: string;
	requesterId: string;
	board: string;
	iosVersion: string | null;
	what: string;
	channelId: string;
	/** Known before this row is ever written: the channel post happens
	 *  first, and only a successful post reaches this call at all. */
	messageId: string;
	createdAt: number;
	candidates: readonly CandidateInput[];
}

const INSERT_REQUEST = `
INSERT INTO test_requests (id, requester_id, board, ios_version, what, status, channel_id, message_id, created_at)
VALUES (?1, ?2, ?3, ?4, ?5, 'pending', ?6, ?7, ?8)
`;

const INSERT_CANDIDATE = `
INSERT INTO test_request_candidates (request_id, discord_user_id, awake_at_request, pinged_awake)
VALUES (?1, ?2, ?3, ?3)
`;

/** Creates the request row and every candidate row in one D1 batch, so a
 *  request never exists with a partial candidate list. `pinged_awake` is
 *  seeded from `awake` itself: an awake candidate is pinged the moment the
 *  request is posted, so there is nothing left to mark after the fact. */
export async function createTestRequest(db: D1Database | undefined, req: NewTestRequest): Promise<void> {
	await run(async () => {
		const bound = need(db)
			.prepare(INSERT_REQUEST)
			.bind(req.id, req.requesterId, req.board, req.iosVersion, req.what, req.channelId, req.messageId, req.createdAt);
		const candidateStmts = req.candidates.map((c) =>
			need(db).prepare(INSERT_CANDIDATE).bind(req.id, c.discordUserId, c.awake ? 1 : 0),
		);
		await need(db).batch([bound, ...candidateStmts]);
	});
}

const SET_THREAD = `UPDATE test_requests SET thread_id = ?2 WHERE id = ?1`;

export async function setThread(db: D1Database | undefined, requestId: string, threadId: string): Promise<void> {
	await run(() => need(db).prepare(SET_THREAD).bind(requestId, threadId).run());
}

const CLAIM = `UPDATE test_requests SET status = 'claimed', claimed_by = ?2 WHERE id = ?1 AND status = 'pending'`;

/** Atomic first-accept-wins: a single UPDATE with the guard in its own WHERE
 *  clause rather than a read-then-write, so two simultaneous Accept clicks
 *  cannot both succeed (same pattern as cooldown.ts's checkMatrixCooldown).
 *  Returns whether this call was the one that claimed it. */
export async function claim(db: D1Database | undefined, requestId: string, userId: string): Promise<boolean> {
	return run(async () => {
		const res = await need(db).prepare(CLAIM).bind(requestId, userId).run();
		return (res.meta?.changes ?? 0) > 0;
	});
}

const GET_REQUEST_BY_THREAD = `
SELECT id, requester_id, board, ios_version, what, status, channel_id, message_id, claimed_by, thread_id, escalated_at, created_at
FROM test_requests WHERE thread_id = ?1
`;

/** `/test-result` runs inside the claim thread, not the queue channel, so it
 *  has to work backwards from "which request does this thread belong to". */
export async function getRequestByThreadId(db: D1Database | undefined, threadId: string): Promise<TestRequestRow | null> {
	return run(async () => (await need(db).prepare(GET_REQUEST_BY_THREAD).bind(threadId).first<TestRequestRow>()) ?? null);
}

const MARK_DONE = `UPDATE test_requests SET status = 'done' WHERE id = ?1 AND status = 'claimed'`;

/** Atomic claimed->done guard, same first-writer-wins shape as `claim()` —
 *  a double /test-result submission (a retry, a duplicate interaction
 *  delivery) writes at most one validation row per call site that checks
 *  this return value. */
export async function markDone(db: D1Database | undefined, requestId: string): Promise<boolean> {
	return run(async () => {
		const res = await need(db).prepare(MARK_DONE).bind(requestId).run();
		return (res.meta?.changes ?? 0) > 0;
	});
}

const GET_REQUEST = `
SELECT id, requester_id, board, ios_version, what, status, channel_id, message_id, claimed_by, thread_id, escalated_at, created_at
FROM test_requests WHERE id = ?1
`;

export async function getRequest(db: D1Database | undefined, requestId: string): Promise<TestRequestRow | null> {
	return run(async () => (await need(db).prepare(GET_REQUEST).bind(requestId).first<TestRequestRow>()) ?? null);
}

const PENDING_UNESCALATED = `
SELECT id, requester_id, board, ios_version, what, status, channel_id, message_id, claimed_by, thread_id, escalated_at, created_at
FROM test_requests
WHERE status = 'pending' AND escalated_at IS NULL AND created_at <= ?1
`;

/** Requests that have sat pending, unescalated, since before `cutoffMs` —
 *  the scheduled sweep's candidate set for waking the asleep half of the
 *  candidate list. */
export async function pendingUnescalated(db: D1Database | undefined, cutoffMs: number): Promise<TestRequestRow[]> {
	return run(async () => {
		const res = await need(db).prepare(PENDING_UNESCALATED).bind(cutoffMs).all<TestRequestRow>();
		return res.results ?? [];
	});
}

const ASLEEP_UNPINGED = `
SELECT discord_user_id FROM test_request_candidates
WHERE request_id = ?1 AND awake_at_request = 0 AND pinged_asleep = 0
`;

export async function asleepUnpingedCandidates(db: D1Database | undefined, requestId: string): Promise<string[]> {
	return run(async () => {
		const res = await need(db).prepare(ASLEEP_UNPINGED).bind(requestId).all<{ discord_user_id: string }>();
		return (res.results ?? []).map((r) => r.discord_user_id);
	});
}

const MARK_ESCALATED = `UPDATE test_requests SET escalated_at = ?2 WHERE id = ?1`;
const MARK_PINGED_ASLEEP = `
UPDATE test_request_candidates SET pinged_asleep = 1 WHERE request_id = ?1 AND discord_user_id = ?2
`;

/** Marks the request escalated and every named candidate as pinged, in one
 *  batch — the sweep must not re-ping the same asleep candidates on its next
 *  tick just because one write succeeded and the other did not. */
export async function markEscalated(
	db: D1Database | undefined,
	requestId: string,
	now: number,
	pingedUserIds: readonly string[],
): Promise<void> {
	await run(async () => {
		const stmts = [
			need(db).prepare(MARK_ESCALATED).bind(requestId, now),
			...pingedUserIds.map((id) => need(db).prepare(MARK_PINGED_ASLEEP).bind(requestId, id)),
		];
		await need(db).batch(stmts);
	});
}
