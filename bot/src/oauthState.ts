/**
 * @file The CSRF/session-correlation state that chains the two OAuth legs
 * ("Discord authorize" then "GitHub authorize") into one flow, and stops a
 * forged callback from attaching a GitHub identity to the wrong Discord
 * user. Every transition is a single guarded statement (same atomic
 * first-writer-wins shape as `claim()` in testRequests.ts), not a
 * read-then-write, so a replayed or duplicated callback cannot advance a
 * state twice.
 */

/** How long a half-finished OAuth flow stays completable. Read at state-read
 *  time, and also the cutoff the abandoned-link purge uses, so "the flow can no
 *  longer finish" and "its tokens are deleted" cannot drift apart. */
export const STATE_TTL_MS = 10 * 60 * 1000;

export class OAuthStateUnavailable extends Error {
	constructor(cause?: unknown) {
		super("oauth state unavailable");
		this.name = "OAuthStateUnavailable";
		this.cause = cause;
	}
}

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new OAuthStateUnavailable();
	return db;
}

async function run<T>(fn: () => Promise<T>): Promise<T> {
	try {
		return await fn();
	} catch (err) {
		throw err instanceof OAuthStateUnavailable ? err : new OAuthStateUnavailable(err);
	}
}

const BEGIN = `INSERT INTO oauth_states (state, stage, created_at) VALUES (?1, 'discord', ?2)`;

/** Starts the flow: a fresh random state, staged "discord", not yet tied to
 *  any Discord user (that only becomes known once the Discord leg's
 *  callback runs). */
export async function beginDiscordLeg(db: D1Database | undefined, now: number = Date.now()): Promise<string> {
	const state = crypto.randomUUID();
	await run(() => need(db).prepare(BEGIN).bind(state, now).run());
	return state;
}

const ADVANCE = `
UPDATE oauth_states SET stage = 'github', discord_user_id = ?2
WHERE state = ?1 AND stage = 'discord' AND created_at >= ?3
`;

/** The Discord callback's job: only succeeds once, only within the TTL of
 *  the original `beginDiscordLeg` call, and only from stage "discord" —
 *  a state cannot be advanced twice, so a replayed callback is a no-op. */
export async function advanceToGithubLeg(
	db: D1Database | undefined,
	state: string,
	discordUserId: string,
	now: number = Date.now(),
): Promise<boolean> {
	return run(async () => {
		const res = await need(db).prepare(ADVANCE).bind(state, discordUserId, now - STATE_TTL_MS).run();
		return (res.meta?.changes ?? 0) > 0;
	});
}

const SELECT_FOR_CONSUME = `
SELECT discord_user_id FROM oauth_states WHERE state = ?1 AND stage = 'github' AND created_at >= ?2
`;
const DELETE_STATE = `DELETE FROM oauth_states WHERE state = ?1`;

/** The GitHub callback's job: consumes the state (deletes it, so it cannot
 *  be replayed) and hands back which Discord user this GitHub identity
 *  belongs to, or null if the state is unknown, expired, or was never
 *  advanced past the Discord leg.
 *
 *  A plain SELECT then DELETE rather than `DELETE ... RETURNING`: D1's own
 *  docs (developers.cloudflare.com/d1/sql-api/sql-statements/, checked
 *  2026-08-04) do not confirm RETURNING support, and the race this would
 *  close — two callbacks for the exact same unguessable random state,
 *  arriving concurrently — is not worth depending on an unverified SQL
 *  feature for. */
export async function consumeGithubLeg(db: D1Database | undefined, state: string, now: number = Date.now()): Promise<string | null> {
	return run(async () => {
		const row = await need(db)
			.prepare(SELECT_FOR_CONSUME)
			.bind(state, now - STATE_TTL_MS)
			.first<{ discord_user_id: string | null }>();
		if (!row) return null;
		await need(db).prepare(DELETE_STATE).bind(state).run();
		return row.discord_user_id;
	});
}
