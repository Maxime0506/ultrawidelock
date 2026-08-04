/**
 * @file The `/matrix` PNG-rendering cooldown.
 *
 * One statement, not a read then a write: SQLite serializes writes to one
 * database, so the WHERE guard on the UPDATE is the atomicity. Two
 * concurrent `/matrix` calls from the same user landing in the same
 * millisecond still only let one through, because there is no gap between
 * reading the old timestamp and writing the new one for a second statement
 * to land in.
 */

const UPSERT = `
INSERT INTO matrix_cooldowns (discord_user_id, last_render_at) VALUES (?1, ?2)
ON CONFLICT (discord_user_id) DO UPDATE SET last_render_at = excluded.last_render_at
WHERE matrix_cooldowns.last_render_at <= excluded.last_render_at - ?3
`;

export type CooldownCheck = { ready: true } | { ready: false; remainingMs: number };

/**
 * Checks and, if expired, atomically starts a new cooldown window in the
 * same statement. A missing or unreachable D1 binding fails *open* here
 * (nobody is rate-limited): rate limiting is a cost control on the PNG
 * path, not a correctness guarantee, and `matrixCounts()` — called right
 * after this in commands/matrix.ts — is what actually surfaces "the
 * registry is not reachable" if D1 is genuinely down.
 */
export async function checkMatrixCooldown(
	db: D1Database | undefined,
	userId: string,
	cooldownMs: number,
	now: number,
): Promise<CooldownCheck> {
	if (!db) return { ready: true };
	try {
		const res = await db.prepare(UPSERT).bind(userId, now, cooldownMs).run();
		if ((res.meta?.changes ?? 0) > 0) return { ready: true };

		const row = await db
			.prepare(`SELECT last_render_at FROM matrix_cooldowns WHERE discord_user_id = ?1`)
			.bind(userId)
			.first<{ last_render_at: number }>();
		const elapsed = row ? now - row.last_render_at : 0;
		return { ready: false, remainingMs: Math.max(0, cooldownMs - elapsed) };
	} catch {
		return { ready: true };
	}
}
