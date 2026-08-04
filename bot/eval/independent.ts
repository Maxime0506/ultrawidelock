/**
 * @file Validate and score a golden set this session did not write.
 *
 * Every number in this directory rests on 183 questions written by the same
 * agent that then graded them, which is the one weakness the harness cannot
 * measure about itself. A question written by somebody who has just read the
 * answering line tends to share vocabulary with it, and lexical retrieval is
 * exactly the technique that reward biases like that. So the headline finding —
 * identifier phrasing retrieves at 0.93, prose phrasing at 0.38 — could in
 * principle be an artifact of how the questions were phrased rather than a fact
 * about the repository.
 *
 * `independent.jsonl` holds 90 questions written by three separate agents, each
 * scoped to one slice of the tree and each told to draft its questions from an
 * imagined situation BEFORE opening any file that might answer them, so the
 * wording could not be copied off the line being cited. None of them read
 * `bot/eval/`. This script then:
 *
 *   1. rejects every anchor that does not resolve, so a hallucinated path or a
 *      misquoted substring cannot enter the measurement,
 *   2. classifies each question mechanically as identifier-phrased or
 *      prose-phrased, by whether it contains a token the retriever can match
 *      exactly, and
 *   3. scores the same retrieval stack on both sets under the same classifier.
 *
 * Blindness here is by instruction, not by sandbox. It cannot be proved from
 * inside, and the honest reason to believe it is the result: an agent that had
 * read the answer key would not have produced a set the harness scores 0.34
 * lower.
 *
 * Usage: node eval/independent.ts [dir]
 *   no argument   score the committed independent.jsonl
 *   a directory   score every indep-*.jsonl in it, for vetting a fresh batch
 *                 before merging it in
 */
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { loadGolden, resolveAnchor, type GoldAnchor } from "./golden.ts";
import { buildChunks, covers, indexableFiles, type Chunk } from "./corpus.ts";
import {
	buildFtsIndex,
	ftsSearch,
	ripgrepSearch,
	chunkLocator,
	corpusFiles,
	terms,
} from "./retrieve.ts";
import { expand } from "./expand.ts";
import { withHeaders } from "./headers.ts";

const K = 10;

/** The committed set. Pass a directory argument instead to score a fresh batch
 *  of `indep-*.jsonl` before deciding whether to merge it in. */
const COMMITTED = new URL("./independent.jsonl", import.meta.url).pathname;

interface RawQuestion {
	id: string;
	q: string;
	gold: GoldAnchor[];
	why?: string;
}

interface Rejection {
	id: string;
	file: string;
	reason: string;
}

/**
 * Identifier-phrased means: the question contains at least one token the lexical
 * retriever can match as an exact string. These are the same six shapes
 * `terms()` extracts verbatim before it falls back to lowercased words, so the
 * classifier asks precisely "did the asker hand the index a literal to match".
 */
const EXACT_SHAPES = [
	/\bCONFIG_[A-Z0-9_]+/,
	/\b0x[0-9A-Fa-f]{2,}\b/,
	/\b[A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+\b/,
	/\b[a-z][a-z0-9-]*\/[\w./-]+/,
	/\bmake\s+[a-z][a-z0-9-]*/,
	/\b[a-z]+[A-Z][A-Za-z0-9]*\b/,
];

function isIdentifierPhrased(question: string): boolean {
	return EXACT_SHAPES.some((re) => re.test(question));
}

function loadRaw(dir: string | undefined): { questions: RawQuestion[]; sources: string[] } {
	const paths = dir
		? readdirSync(dir)
				.filter((f) => /^indep-.*\.jsonl$/.test(f))
				.sort()
				.map((f) => join(dir, f))
		: [COMMITTED];
	const questions: RawQuestion[] = [];
	for (const src of paths) {
		for (const line of readFileSync(src, "utf8").split("\n")) {
			const text = line.trim();
			if (!text || text.startsWith("//")) continue;
			try {
				questions.push(JSON.parse(text) as RawQuestion);
			} catch {
				questions.push({ id: `${src}:unparseable`, q: "", gold: [] });
			}
		}
	}
	return { questions, sources: paths.map((p) => p.split("/").pop() ?? p) };
}

/** Drop anchors that do not resolve, and questions left with none. */
function validate(raw: RawQuestion[]): { kept: RawQuestion[]; rejected: Rejection[] } {
	const indexed = new Set(indexableFiles());
	const kept: RawQuestion[] = [];
	const rejected: Rejection[] = [];

	for (const q of raw) {
		if (!q.q || !Array.isArray(q.gold) || q.gold.length === 0) {
			rejected.push({ id: q.id, file: "-", reason: "no question or no anchors" });
			continue;
		}
		const good: GoldAnchor[] = [];
		for (const a of q.gold) {
			if (!a || typeof a.file !== "string" || typeof a.expect !== "string") {
				rejected.push({ id: q.id, file: String(a?.file), reason: "malformed anchor" });
				continue;
			}
			if (!indexed.has(a.file)) {
				rejected.push({ id: q.id, file: a.file, reason: "not an indexed file" });
				continue;
			}
			const hits = resolveAnchor(a).lines;
			if (hits.length === 0) {
				rejected.push({ id: q.id, file: a.file, reason: "expect string not found" });
				continue;
			}
			if (hits.length > 1) {
				rejected.push({ id: q.id, file: a.file, reason: `ambiguous, ${hits.length} lines` });
				continue;
			}
			good.push(a);
		}
		if (good.length > 0) kept.push({ ...q, gold: good });
	}
	return { kept, rejected };
}

const mean = (xs: number[]) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Number.NaN);

interface Scorer {
	(question: string, gold: GoldAnchor[]): { r10: number; r5: number; mrr: number };
}

function makeScorer(chunks: Chunk[], files: string[], xf: (q: string) => string): Scorer {
	const locate = chunkLocator(chunks);
	const fts = buildFtsIndex(chunks, false);
	return (question, gold) => {
		// Same RRF fusion the Stage 1 probe uses, so the two sets are scored by
		// one retrieval stack and differ only in who wrote the questions.
		const lists = [ftsSearch(fts, xf(question), K), ripgrepSearch(files, locate, xf(question), K)];
		const rrf = new Map<string, { chunk: Chunk; score: number }>();
		for (const list of lists) {
			list.forEach((h, i) => {
				const key = `${h.chunk.file}:${h.chunk.start}`;
				const prev = rrf.get(key);
				const add = 1 / (60 + i + 1);
				if (prev) prev.score += add;
				else rrf.set(key, { chunk: h.chunk, score: add });
			});
		}
		const hits = [...rrf.values()].sort(
			(a, b) =>
				b.score - a.score ||
				a.chunk.file.localeCompare(b.chunk.file) ||
				a.chunk.start - b.chunk.start,
		);
		const anchors = gold.map(resolveAnchor);
		const hitAt = (n: number) =>
			anchors.filter((a) =>
				hits.slice(0, n).some((h) => a.lines.some((l) => covers(h.chunk, a.file, l))),
			).length / anchors.length;
		let rank = 0;
		for (let i = 0; i < hits.length; i++) {
			if (anchors.some((a) => a.lines.some((l) => covers(hits[i].chunk, a.file, l)))) {
				rank = i + 1;
				break;
			}
		}
		return { r10: hitAt(K), r5: hitAt(5), mrr: rank ? 1 / rank : 0 };
	};
}

interface Row {
	label: string;
	n: number;
	r10: number;
	r5: number;
	mrr: number;
}

function summarise(
	label: string,
	items: { q: string; gold: GoldAnchor[] }[],
	score: Scorer,
): Row[] {
	const scored = items.map((it) => ({ ...score(it.q, it.gold), ident: isIdentifierPhrased(it.q) }));
	const slice = (name: string, xs: typeof scored): Row => ({
		label: name,
		n: xs.length,
		r10: mean(xs.map((s) => s.r10)),
		r5: mean(xs.map((s) => s.r5)),
		mrr: mean(xs.map((s) => s.mrr)),
	});
	return [
		slice(`${label}, all`, scored),
		slice(`${label}, identifier-phrased`, scored.filter((s) => s.ident)),
		slice(`${label}, prose-phrased`, scored.filter((s) => !s.ident)),
	];
}

function table(rows: Row[]): void {
	console.log("| set | n | recall@10 | recall@5 | MRR |");
	console.log("|---|---:|---:|---:|---:|");
	for (const r of rows) {
		console.log(
			`| ${r.label} | ${r.n} | ${r.r10.toFixed(3)} | ${r.r5.toFixed(3)} | ${r.mrr.toFixed(3)} |`,
		);
	}
	console.log("");
}

/**
 * Where do the misses actually fail?
 *
 * This is the question the Stage 0 README deferred, and it decides whether the
 * Stage 2 dense-retrieval layer gets built. Every anchor lands in one bucket:
 *
 *   hit           in the top 10 the bot would actually serve
 *   buried        missed at 10, but present in the top 200 of a deeper search —
 *                 the lexical index CAN see it and the ranking buries it, which
 *                 a reranker fixes and embeddings are not needed for
 *   unreachable   absent even from 200 — the query and the answering line share
 *                 too little for the index to surface it at all, the vocabulary
 *                 gap, and the only honest argument for embeddings
 *
 * Two traps, both hit while writing this and both worth stating, since the
 * bucket split is the number the Stage 2 decision rests on:
 *
 *   RRF is depth-dependent. A chunk ranked 15th by FTS5 and 3rd by ripgrep gets
 *   only ripgrep's contribution when each list is cut at 10, and both when they
 *   are cut at 200, so the fused order genuinely differs. "Rank <= 10 in a
 *   depth-200 fusion" is therefore NOT the top 10 the bot serves. Hits are
 *   decided by the same depth-10 fusion the headline table uses, and only the
 *   misses are looked up in the deep list.
 *
 *   Fusing two lists of 200 yields up to 400 entries, so the deep list must be
 *   sliced to 200 before "not in 200" means anything.
 *
 * These counts are micro-averaged over anchors, whereas the headline table is
 * macro-averaged over questions, so `hit/total` here differs slightly from the
 * recall@10 above. The overlap column counts how many of the query's extracted
 * terms appear in the answering chunk, so "unreachable" can be checked rather
 * than assumed.
 */
function diagnose(
	label: string,
	items: { q: string; gold: GoldAnchor[] }[],
	chunks: Chunk[],
	files: string[],
): void {
	const DEEP = 200;
	const locate = chunkLocator(chunks);
	const fts = buildFtsIndex(chunks, false);
	const buckets = { hit: 0, buried: 0, unreachable: 0 };
	const overlaps: number[] = [];

	const fuse = (question: string, depth: number): Chunk[] => {
		const lists = [ftsSearch(fts, question, depth), ripgrepSearch(files, locate, question, depth)];
		const rrf = new Map<string, { chunk: Chunk; score: number }>();
		for (const list of lists) {
			list.forEach((h, i) => {
				const key = `${h.chunk.file}:${h.chunk.start}`;
				const prev = rrf.get(key);
				const add = 1 / (60 + i + 1);
				if (prev) prev.score += add;
				else rrf.set(key, { chunk: h.chunk, score: add });
			});
		}
		return [...rrf.values()]
			.sort(
				(a, b) =>
					b.score - a.score ||
					a.chunk.file.localeCompare(b.chunk.file) ||
					a.chunk.start - b.chunk.start,
			)
			.slice(0, depth)
			.map((h) => h.chunk);
	};

	for (const item of items) {
		const shallow = fuse(item.q, K);
		const deep = fuse(item.q, DEEP);
		const qTerms = terms(item.q).map((t) => t.toLowerCase());

		for (const anchor of item.gold.map(resolveAnchor)) {
			const seen = (list: Chunk[]) =>
				list.some((c) => anchor.lines.some((l) => covers(c, anchor.file, l)));
			if (seen(shallow)) buckets.hit++;
			else if (seen(deep)) buckets.buried++;
			else {
				buckets.unreachable++;
				const gold = anchor.lines
					.map((l) => locate(anchor.file, l))
					.find((c): c is Chunk => c !== undefined);
				const body = (gold?.text ?? "").toLowerCase();
				overlaps.push(qTerms.filter((t) => body.includes(t)).length);
			}
		}
	}

	const total = buckets.hit + buckets.buried + buckets.unreachable;
	const pct = (n: number) => `${n} (${((100 * n) / total).toFixed(0)}%)`;
	console.log(
		`| ${label} | ${total} | ${pct(buckets.hit)} | ${pct(buckets.buried)} | ` +
			`${pct(buckets.unreachable)} | ${mean(overlaps).toFixed(1)} |`,
	);
}

function main(): void {
	const dir = process.argv[2];
	const { questions: raw, sources } = loadRaw(dir);
	if (raw.length === 0) {
		console.log(`no questions found in ${dir ?? COMMITTED}`);
		process.exit(1);
	}

	const { kept, rejected } = validate(raw);
	console.log(`independent set: ${sources.join(", ")}\n`);
	console.log(
		`${raw.length} questions proposed, ${kept.length} kept, ` +
			`${rejected.length} anchors rejected\n`,
	);

	if (rejected.length > 0) {
		console.log("| id | file | why rejected |");
		console.log("|---|---|---|");
		for (const r of rejected) console.log(`| ${r.id} | ${r.file} | ${r.reason} |`);
		console.log("");
	}

	const chunks = buildChunks();
	const files = corpusFiles();
	const mine = loadGolden().filter((q) => q.stratum !== "negative");

	// The independent agents were each scoped to a slice of the tree, so a
	// straight comparison against the whole self-written set compares topic mixes
	// as much as authors. `firmware/` and `mk/` are the config-heavy areas that
	// are already the self-written set's worst stratum, so that comparison would
	// flatter nobody honestly. Restrict the self-written set to questions whose
	// anchors all land in the same top-level areas, and the remaining difference
	// is authorship.
	const areas = new Set(kept.flatMap((q) => q.gold.map((a) => a.file.split("/")[0])));
	const matched = mine.filter((q) => q.gold.every((a) => areas.has(a.file.split("/")[0])));
	console.log(`self-written subset matched on areas {${[...areas].sort().join(", ")}}\n`);

	// Headers are re-tested here even though the self-written set ruled them
	// dominated, because that verdict was reached without any question of the
	// shape the independent set is full of.
	const headed = withHeaders(chunks).map((c, i) => ({ ...c, id: i }));

	const variants: [string, Chunk[], (q: string) => string][] = [
		["naive chunks, no expansion", chunks, (q) => q],
		["naive chunks, query expansion", chunks, expand],
		["deterministic headers, no expansion", headed, (q) => q],
		["deterministic headers + query expansion", headed, expand],
	];

	for (const [name, cs, xf] of variants) {
		const score = makeScorer(cs, files, xf);
		console.log(`### ${name}\n`);
		table([
			...summarise("independent", kept, score),
			...summarise("self-written, same areas", matched, score),
			...summarise("self-written, whole repo", mine, score),
		]);
	}

	console.log("### where the misses fail, retrieving 200 deep\n");
	console.log(
		"| set | anchors | in top 10 | buried 11-200 | not in 200 | terms shared when unreachable |",
	);
	console.log("|---|---:|---:|---:|---:|---:|");
	diagnose("independent, naive chunks", kept, chunks, files);
	diagnose("independent, headers", kept, headed, files);
	diagnose("self-written same areas, naive chunks", matched, chunks, files);
	console.log("");
}

main();
