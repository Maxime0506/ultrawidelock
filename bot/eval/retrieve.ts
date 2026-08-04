/**
 * @file The two lexical retrievers Stage 0 measures, over one shared tokenizer.
 *
 * Both baselines take the same term list, so a difference between them is a
 * difference in the index and not in how the question was read.
 *
 * The FTS5 index is built twice on purpose. SQLite's unicode61 tokenizer treats
 * `_` as a separator, which shreds `CONFIG_UART_CONSOLE` into three ordinary
 * English words and makes the single most common query shape in this repo
 * un-retrievable. `tokenchars '_'` fixes it. Measuring both quantifies how much
 * of "lexical search works here" is really "lexical search configured for
 * identifiers works here".
 */
import { DatabaseSync } from "node:sqlite";
import { execFileSync } from "node:child_process";
import { REPO_ROOT } from "./golden.ts";
import { type Chunk, indexableFiles } from "./corpus.ts";

/** Function words only. Domain words like "make", "flash" and "build" are left
 *  in: they are real tokens here, and idf weighting already discounts them. */
const STOP = new Set(
	("a an the is are was were be been being do does did doing i me my we our you your it its" +
		" this that these those there their they them he she his her as at by for from in into of" +
		" on or and to with without not no yes if then than so out up down over under again" +
		" what when where which who whom why how can could should would will shall may might must" +
		" have has had having get gets got getting say says said just about after before does" +
		" me mine ours yours am does isn't don't doesn't didn't")
		.split(/\s+/)
		.filter(Boolean),
);

/**
 * Query terms, exact tokens preserved.
 *
 * Order matters only for readability; scoring is by idf, not position.
 */
export function terms(question: string): string[] {
	const out = new Set<string>();
	const keep = (t: string) => {
		if (t.length >= 2) out.add(t);
	};

	for (const m of question.matchAll(/\bCONFIG_[A-Z0-9_]+/g)) keep(m[0]);
	for (const m of question.matchAll(/\b0x[0-9A-Fa-f]{2,}\b/g)) keep(m[0]);
	for (const m of question.matchAll(/\b[A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+\b/g)) keep(m[0]);
	for (const m of question.matchAll(/\b[a-z][a-z0-9-]*\/[\w./-]+/g)) keep(m[0]);
	for (const m of question.matchAll(/\bmake\s+([a-z][a-z0-9-]*)/g)) keep(m[1]);
	for (const m of question.matchAll(/\b[a-z]+[A-Z][A-Za-z0-9]*\b/g)) keep(m[0]);

	for (const m of question.matchAll(/[A-Za-z][A-Za-z0-9'-]{2,}/g)) {
		const w = m[0].toLowerCase();
		if (!STOP.has(w)) keep(w);
	}
	return [...out];
}

export interface Hit {
	chunk: Chunk;
	score: number;
}

// ---------------------------------------------------------------- FTS5

export interface FtsIndex {
	db: DatabaseSync;
	/** rowid -> chunk. A Map rather than an array index, because a filtered chunk
	 *  list no longer has chunk.id equal to its position and the array form then
	 *  silently returned undefined. */
	byRowid: Map<number, Chunk>;
}

export function buildFtsIndex(chunks: Chunk[], underscoreAware: boolean): FtsIndex {
	const db = new DatabaseSync(":memory:");
	const tokenize = underscoreAware ? `, tokenize = "unicode61 tokenchars '_'"` : "";
	db.exec(`create virtual table docs using fts5(body${tokenize})`);
	const insert = db.prepare("insert into docs(rowid, body) values (?, ?)");
	const byRowid = new Map<number, Chunk>();
	db.exec("begin");
	let rowid = 1;
	for (const c of chunks) {
		insert.run(rowid, `${c.file}\n${c.text}`);
		byRowid.set(rowid++, c);
	}
	db.exec("commit");
	return { db, byRowid };
}

export function ftsSearch(index: FtsIndex, question: string, k: number): Hit[] {
	const ts = terms(question);
	if (ts.length === 0) return [];
	// Each term as an FTS5 phrase, so punctuation inside it cannot be read as syntax.
	const match = ts.map((t) => `"${t.replace(/"/g, '""')}"`).join(" OR ");
	let rows: { rowid: number; s: number }[];
	try {
		rows = index.db
			.prepare("select rowid, bm25(docs) as s from docs where docs match ? order by s limit ?")
			.all(match, k) as unknown as { rowid: number; s: number }[];
	} catch {
		return [];
	}
	// bm25() is more negative for a better match; flip it so higher is better.
	return rows.flatMap((r) => {
		const chunk = index.byRowid.get(r.rowid);
		return chunk ? [{ chunk, score: -r.s }] : [];
	});
}

// ------------------------------------------------------------- ripgrep

/** Chunk lookup by file, for turning a rg `file:line` into the chunk holding it. */
export function chunkLocator(chunks: Chunk[]): (file: string, line: number) => Chunk | undefined {
	const byFile = new Map<string, Chunk[]>();
	for (const c of chunks) {
		let list = byFile.get(c.file);
		if (!list) byFile.set(c.file, (list = []));
		list.push(c);
	}
	// Overlapping windows mean a line sits in up to two chunks; take the first,
	// so a hit is attributed to exactly one chunk and cannot be double counted.
	return (file, line) => byFile.get(file)?.find((c) => line >= c.start && line <= c.end);
}

export function ripgrepSearch(
	files: string[],
	locate: (file: string, line: number) => Chunk | undefined,
	question: string,
	k: number,
): Hit[] {
	const ts = terms(question);
	if (ts.length === 0) return [];

	const args = ["--no-heading", "--line-number", "--only-matching", "--ignore-case", "--fixed-strings"];
	for (const t of ts) args.push("-e", t);
	args.push("--", ...files);

	let out = "";
	try {
		out = execFileSync("rg", args, {
			cwd: REPO_ROOT,
			encoding: "utf8",
			maxBuffer: 256 * 1024 * 1024,
		});
	} catch (e) {
		// rg exits 1 when nothing matched, which is a legitimate empty result.
		const err = e as { status?: number; stdout?: string };
		if (err.status !== 1) throw e;
		out = err.stdout ?? "";
	}

	// chunk id -> set of distinct terms that matched inside it
	const perChunk = new Map<number, Set<string>>();
	const chunkOf = new Map<number, Chunk>();
	const docFreq = new Map<string, Set<number>>();

	for (const line of out.split("\n")) {
		if (!line) continue;
		const first = line.indexOf(":");
		const second = line.indexOf(":", first + 1);
		if (first < 0 || second < 0) continue;
		const file = line.slice(0, first);
		const lineNo = Number(line.slice(first + 1, second));
		const matched = line.slice(second + 1).toLowerCase();
		if (!Number.isFinite(lineNo)) continue;
		const chunk = locate(file, lineNo);
		if (!chunk) continue;

		let set = perChunk.get(chunk.id);
		if (!set) perChunk.set(chunk.id, (set = new Set()));
		set.add(matched);
		chunkOf.set(chunk.id, chunk);

		let df = docFreq.get(matched);
		if (!df) docFreq.set(matched, (df = new Set()));
		df.add(chunk.id);
	}

	const n = Math.max(perChunk.size, 1);
	const hits: Hit[] = [];
	for (const [id, matchedTerms] of perChunk) {
		let score = 0;
		for (const t of matchedTerms) {
			const df = docFreq.get(t)?.size ?? 1;
			score += Math.log(1 + n / df);
		}
		hits.push({ chunk: chunkOf.get(id)!, score });
	}
	// rg walks files on several threads, so its output order is not stable between
	// runs. Ties broken on score alone made recall@5 move by 0.07 across identical
	// runs, which would be a CI gate that flaps. Break ties on the chunk's own
	// identity instead, so the ranking depends only on the index.
	hits.sort(
		(a, b) =>
			b.score - a.score ||
			a.chunk.file.localeCompare(b.chunk.file) ||
			a.chunk.start - b.chunk.start,
	);
	return hits.slice(0, k);
}

export function corpusFiles(): string[] {
	return indexableFiles();
}
