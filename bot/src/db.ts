/**
 * @file Interaction idempotency and the `/build` cooldown.
 *
 * The hardware registry itself lives in rigs.ts, against the `rigs` table.
 * This file is what is left once that moved: the two tables that exist to stop
 * a command running twice rather than to remember anything about hardware.
 *
 * Nothing outside this file and rigs.ts writes SQL, and neither builds SQL by
 * concatenation. Each query is a constant with bound parameters, so a value
 * arriving from a Discord field cannot become syntax.
 */

// One class, imported rather than redeclared. Both this file and rigs.ts throw
// "the registry is down", and two same-named classes in two modules would make
// every `instanceof RegistryUnavailable` in a caller fail depending on which
// module happened to throw — silently, and only when D1 is already broken.
import { RegistryUnavailable } from "./rigs.ts";

export { RegistryUnavailable };

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new RegistryUnavailable();
	return db;
}

const CLAIM = `INSERT OR IGNORE INTO handled_interactions (interaction_id, handled_at) VALUES (?1, ?2)`;

/**
 * Claim an interaction ID, once.
 *
 * True means this caller owns it and should do the work. False means a
 * previous delivery already did: Discord retries, and a retry must not open a
 * second thread.
 *
 * Throws RegistryUnavailable when D1 is not reachable, and the caller is
 * expected to proceed anyway. Losing deduplication costs a duplicate thread;
 * refusing to act costs the report entirely, and the second is worse.
 */
export async function claimInteraction(
	db: D1Database | undefined,
	interactionId: string,
): Promise<boolean> {
	try {
		const res = await need(db).prepare(CLAIM).bind(interactionId, Date.now()).run();
		return (res.meta?.changes ?? 0) > 0;
	} catch (err) {
		throw err instanceof RegistryUnavailable ? err : new RegistryUnavailable(err);
	}
}

// A guarded upsert, not a select-then-write. SQLite serializes writes to one
// database, so this one statement is the atomicity: either the WHERE clause
// admits the update and `changes` comes back 1, or it does not and nothing
// moved. Two /build calls from the same user landing on the same millisecond
// still only let one of them through, because there is no gap between the
// read of the old timestamp and the write of the new one for a second
// statement to land in.
const COOLDOWN_UPSERT = `
INSERT INTO build_cooldowns (user_id, last_dispatch_at) VALUES (?1, ?2)
ON CONFLICT (user_id) DO UPDATE SET last_dispatch_at = excluded.last_dispatch_at
WHERE build_cooldowns.last_dispatch_at <= excluded.last_dispatch_at - ?3
`;
const COOLDOWN_LOOKUP = `SELECT last_dispatch_at FROM build_cooldowns WHERE user_id = ?1`;

export type CooldownCheck = { ready: true } | { ready: false; remainingMs: number };

/** Check a user's /build cooldown and, if it has expired, start a new one
 *  atomically in the same statement. */
export async function checkAndStartCooldown(
	db: D1Database | undefined,
	userId: string,
	cooldownMs: number,
	now: number,
): Promise<CooldownCheck> {
	try {
		const bound = need(db);
		const res = await bound.prepare(COOLDOWN_UPSERT).bind(userId, now, cooldownMs).run();
		if ((res.meta?.changes ?? 0) > 0) return { ready: true };

		// The guard held: still cooling down. This second read is not part of
		// the enforcement, only of the message — a race here could report a
		// stale remaining time by a few milliseconds, never let a second
		// dispatch through.
		const row = await bound
			.prepare(COOLDOWN_LOOKUP)
			.bind(userId)
			.first<{ last_dispatch_at: number }>();
		const elapsed = row ? now - row.last_dispatch_at : 0;
		return { ready: false, remainingMs: Math.max(0, cooldownMs - elapsed) };
	} catch (err) {
		throw err instanceof RegistryUnavailable ? err : new RegistryUnavailable(err);
	}
}
