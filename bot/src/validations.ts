/**
 * @file Every statement this Worker runs against `validations`. Same rule
 * as rigs.ts and testRequests.ts: no SQL built by concatenation, every
 * value a bound parameter.
 */
import type { ValidationStatus } from "./matrix.ts";

/** Thrown when the D1 binding is missing or a statement fails. */
export class ValidationsUnavailable extends Error {
	constructor(cause?: unknown) {
		super("validations unavailable");
		this.name = "ValidationsUnavailable";
		this.cause = cause;
	}
}

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new ValidationsUnavailable();
	return db;
}

async function run<T>(fn: () => Promise<T>): Promise<T> {
	try {
		return await fn();
	} catch (err) {
		throw err instanceof ValidationsUnavailable ? err : new ValidationsUnavailable(err);
	}
}

export interface NewValidation {
	id: string;
	board: string;
	iosVersion: string;
	passed: boolean;
	testedBy: string;
	requestId: string | null;
	testedAt: number;
}

const INSERT_VALIDATION = `
INSERT INTO validations (id, board, ios_version, passed, tested_by, request_id, tested_at)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
`;

/** Records one test event. Never overwrites a prior result for the same
 *  pair — a re-test is a new row, so the history survives even though
 *  `/matrix` only ever reads the latest. */
export async function recordValidation(db: D1Database | undefined, v: NewValidation): Promise<void> {
	await run(() =>
		need(db)
			.prepare(INSERT_VALIDATION)
			.bind(v.id, v.board, v.iosVersion, v.passed ? 1 : 0, v.testedBy, v.requestId, v.testedAt)
			.run(),
	);
}

const LATEST_VALIDATIONS = `
SELECT v.board, v.ios_version, v.passed
FROM validations v
INNER JOIN (
	SELECT board, ios_version, MAX(tested_at) AS max_tested_at
	FROM validations
	GROUP BY board, ios_version
) latest
	ON v.board = latest.board
	AND v.ios_version = latest.ios_version
	AND v.tested_at = latest.max_tested_at
`;

const COUNT_BY_TESTER = `SELECT COUNT(*) AS n FROM validations WHERE tested_by = ?1`;

/** How many test results one person has recorded — the Linked Roles
 *  `validated_runs` metadata field. */
export async function countValidationsByTester(db: D1Database | undefined, userId: string): Promise<number> {
	return run(async () => {
		const row = await need(db).prepare(COUNT_BY_TESTER).bind(userId).first<{ n: number }>();
		return row?.n ?? 0;
	});
}

/** The most recent result per (board, ios_version) — what `/matrix` shows
 *  as ✅/❌. A dead-tie on `tested_at` for the same pair (two rows written
 *  at the exact same millisecond) can duplicate a pair in the result; that
 *  is a display redundancy, not a wrong glyph, since every caller reads
 *  this with `.find()` and takes the first match either way. */
export async function latestValidations(db: D1Database | undefined): Promise<ValidationStatus[]> {
	return run(async () => {
		const res = await need(db)
			.prepare(LATEST_VALIDATIONS)
			.all<{ board: string; ios_version: string; passed: number }>();
		return (res.results ?? []).map((r) => ({ board: r.board, iosVersion: r.ios_version, passed: r.passed === 1 }));
	});
}
