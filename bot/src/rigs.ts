/**
 * @file Every statement this Worker runs against D1.
 *
 * Nothing outside this file writes SQL, and nothing in this file builds SQL
 * by concatenation. Each query is a constant with bound parameters, so a
 * value arriving from a Discord field cannot become syntax.
 */

export interface RigRow {
	discord_user_id: string;
	board: string;
	radio: string;
	nfc: string;
	phone_model: string | null;
	ios_version: string | null;
	probe_serial: string | null;
	utc_offset: number;
	awake_start: number;
	awake_end: number;
	updated_at: number;
}

/** Thrown when the D1 binding is missing or a statement fails, so callers
 *  can say "the registry is down" rather than "something went wrong". */
export class RegistryUnavailable extends Error {
	constructor(cause?: unknown) {
		super("registry unavailable");
		this.name = "RegistryUnavailable";
		this.cause = cause;
	}
}

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new RegistryUnavailable();
	return db;
}

async function run<T>(fn: () => Promise<T>): Promise<T> {
	try {
		return await fn();
	} catch (err) {
		throw err instanceof RegistryUnavailable ? err : new RegistryUnavailable(err);
	}
}

const UPSERT = `
INSERT INTO rigs (
	discord_user_id, board, radio, nfc, phone_model, ios_version, probe_serial,
	utc_offset, awake_start, awake_end, updated_at
)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
ON CONFLICT (discord_user_id, board) DO UPDATE SET
	radio        = excluded.radio,
	nfc          = excluded.nfc,
	phone_model  = excluded.phone_model,
	ios_version  = excluded.ios_version,
	probe_serial = excluded.probe_serial,
	utc_offset   = excluded.utc_offset,
	awake_start  = excluded.awake_start,
	awake_end    = excluded.awake_end,
	updated_at   = excluded.updated_at
`;

/** Record what one contributor has for one board. Re-running it for the same
 *  board replaces that entry rather than accumulating rows. */
export async function upsertRig(
	db: D1Database | undefined,
	row: Omit<RigRow, "updated_at" | "probe_serial"> & { probe_serial?: string | null },
): Promise<void> {
	await run(() =>
		need(db)
			.prepare(UPSERT)
			.bind(
				row.discord_user_id,
				row.board,
				row.radio,
				row.nfc,
				row.phone_model,
				row.ios_version,
				row.probe_serial ?? null,
				row.utc_offset,
				row.awake_start,
				row.awake_end,
				Date.now(),
			)
			.run(),
	);
}

const DELETE_ONE = `DELETE FROM rigs WHERE discord_user_id = ?1 AND board = ?2`;
const DELETE_ALL = `DELETE FROM rigs WHERE discord_user_id = ?1`;

/** Hard delete. `board` omitted deletes every board for that user. Returns
 *  how many rows went, so the confirmation can be specific. */
export async function forgetRig(
	db: D1Database | undefined,
	userId: string,
	board?: string,
): Promise<number> {
	return run(async () => {
		const stmt = board
			? need(db).prepare(DELETE_ONE).bind(userId, board)
			: need(db).prepare(DELETE_ALL).bind(userId);
		const res = await stmt.run();
		return res.meta?.changes ?? 0;
	});
}

const ENTRIES_FOR_USER = `
SELECT discord_user_id, board, radio, nfc, phone_model, ios_version, probe_serial,
       utc_offset, awake_start, awake_end, updated_at
FROM rigs WHERE discord_user_id = ?1 ORDER BY board ASC
`;

/** Every board one person has registered. */
export async function entriesForUser(
	db: D1Database | undefined,
	userId: string,
): Promise<RigRow[]> {
	return run(async () => {
		const res = await need(db).prepare(ENTRIES_FOR_USER).bind(userId).all<RigRow>();
		return res.results ?? [];
	});
}

export const WHO_HAS_LIMIT = 50;

/**
 * Owners matching board and/or iOS version, most-recently-updated first.
 * At least one filter is required by the caller (src/commands/who-has.ts);
 * this function itself just runs whichever WHERE clause the filters imply.
 */
export async function whoHas(
	db: D1Database | undefined,
	filter: { board?: string; iosVersion?: string },
): Promise<RigRow[]> {
	const clauses: string[] = [];
	const params: (string | number)[] = [];
	if (filter.board) {
		params.push(filter.board);
		clauses.push(`board = ?${params.length}`);
	}
	if (filter.iosVersion) {
		params.push(filter.iosVersion);
		clauses.push(`ios_version = ?${params.length}`);
	}
	const where = clauses.length > 0 ? `WHERE ${clauses.join(" AND ")}` : "";
	const sql = `
		SELECT discord_user_id, board, radio, nfc, phone_model, ios_version, probe_serial,
		       utc_offset, awake_start, awake_end, updated_at
		FROM rigs ${where}
		ORDER BY updated_at DESC
		LIMIT ${WHO_HAS_LIMIT}
	`;
	return run(async () => {
		const stmt = need(db).prepare(sql);
		const res = await (params.length > 0 ? stmt.bind(...params) : stmt).all<RigRow>();
		return res.results ?? [];
	});
}

export interface MatrixCount {
	board: string;
	ios_version: string;
	n: number;
}

const MATRIX_COUNTS = `
SELECT board, ios_version, COUNT(*) AS n
FROM rigs
WHERE ios_version IS NOT NULL
GROUP BY board, ios_version
`;

/** Owner counts for every (board, ios_version) pair that has at least one
 *  owner. A pair absent from the result has zero owners — src/matrix.ts
 *  fills that in as the "nobody owns this" glyph rather than omitting the
 *  cell, since an empty cell is the entire point of the matrix. */
export async function matrixCounts(db: D1Database | undefined): Promise<MatrixCount[]> {
	return run(async () => {
		const res = await need(db).prepare(MATRIX_COUNTS).all<MatrixCount>();
		return res.results ?? [];
	});
}

const COUNT_FOR_USER = `SELECT COUNT(*) AS n FROM rigs WHERE discord_user_id = ?1`;

/** Used by tests to prove `/forget` leaves no row, and by nothing else. */
export async function countForUser(db: D1Database | undefined, userId: string): Promise<number> {
	return run(async () => {
		const row = await need(db).prepare(COUNT_FOR_USER).bind(userId).first<{ n: number }>();
		return row?.n ?? 0;
	});
}
