/**
 * @file A D1 binding backed by real SQLite, for the tests.
 *
 * Node ships SQLite, and D1 is SQLite, so the rigs tests run the actual
 * migration and the actual statements rather than a hand-written fake that
 * agrees with whatever the code happens to do. A typo in a column name, a
 * broken ON CONFLICT clause or a stray WHERE all fail here.
 *
 * Implements only the slice of the D1 API src/rigs.ts uses: prepare, bind,
 * run, first, all.
 */
import { DatabaseSync } from "node:sqlite";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const MIGRATIONS_DIR = join(import.meta.dirname, "../migrations");

function migrations(): string[] {
	return readdirSync(MIGRATIONS_DIR)
		.filter((f) => f.endsWith(".sql"))
		.sort()
		.map((f) => readFileSync(join(MIGRATIONS_DIR, f), "utf8"));
}

type Param = string | number | null;

class FakeStatement {
	#db: DatabaseSync;
	#sql: string;
	#values: Param[];

	constructor(db: DatabaseSync, sql: string, values: Param[] = []) {
		this.#db = db;
		this.#sql = sql;
		this.#values = values;
	}

	bind(...values: Param[]): FakeStatement {
		return new FakeStatement(this.#db, this.#sql, values);
	}

	async run(): Promise<{ success: true; meta: { changes: number } }> {
		const res = this.#db.prepare(this.#sql).run(...this.#values);
		return { success: true, meta: { changes: Number(res.changes) } };
	}

	async first<T>(): Promise<T | null> {
		return (this.#db.prepare(this.#sql).get(...this.#values) as T) ?? null;
	}

	async all<T>(): Promise<{ results: T[] }> {
		return { results: this.#db.prepare(this.#sql).all(...this.#values) as T[] };
	}
}

export interface FakeD1 {
	binding: unknown;
	rows(sql: string, ...params: Param[]): Record<string, unknown>[];
	close(): void;
}

/** A fresh in-memory registry with the migration applied. */
export function makeD1(): FakeD1 {
	const sqlite = new DatabaseSync(":memory:");
	for (const sql of migrations()) sqlite.exec(sql);
	return {
		binding: {
			prepare: (sql: string) => new FakeStatement(sqlite, sql),
			// Real D1 runs a batch as one implicit transaction; mirrored here
			// with BEGIN/COMMIT so a mid-batch failure leaves nothing applied,
			// same as testRequests.ts's callers assume.
			async batch(stmts: FakeStatement[]) {
				sqlite.exec("BEGIN");
				try {
					const results = [];
					for (const s of stmts) results.push(await s.run());
					sqlite.exec("COMMIT");
					return results;
				} catch (err) {
					sqlite.exec("ROLLBACK");
					throw err;
				}
			},
		},
		rows: (sql, ...params) => sqlite.prepare(sql).all(...params) as Record<string, unknown>[],
		close: () => sqlite.close(),
	};
}

/** A binding that fails on every statement, for the degradation tests. */
export function brokenD1(): unknown {
	return {
		prepare(): never {
			throw new Error("D1_ERROR: network unreachable");
		},
	};
}
