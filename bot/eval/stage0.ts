/**
 * @file Stage 0 baseline D: the lexical floor, measured.
 *
 * Recall@k here is a retrieval metric, so this runs with no API key, no network
 * and no model call. That matters: the Stage 0 gate can be decided for $0, and
 * only the baselines that are somebody else's hosted service cost anything.
 *
 * A gold anchor counts as retrieved when a returned chunk covers the line that
 * actually contains its `expect` substring at HEAD. That is the same test as
 * "the citation resolves to the right lines at a pinned SHA", which is the
 * third weighted stratum, so citation correctness is not scored separately for
 * this baseline — it is what recall already means here.
 */
import { loadGolden, resolveAnchor, headSha, type GoldenQuestion } from "./golden.ts";
import { buildChunks, covers, type Chunk } from "./corpus.ts";
import {
	buildFtsIndex,
	ftsSearch,
	ripgrepSearch,
	chunkLocator,
	corpusFiles,
	type Hit,
} from "./retrieve.ts";

const K = 10;

interface QuestionResult {
	id: string;
	stratum: string;
	recallAtK: number;
	recallAt5: number;
	/** 1-indexed rank of the first chunk covering any gold anchor, 0 if none. */
	firstHitRank: number;
	topScore: number;
	ms: number;
}

function scoreQuestion(q: GoldenQuestion, hits: Hit[], ms: number): QuestionResult {
	const anchors = q.gold.map(resolveAnchor);
	const hitAt = (n: number) =>
		anchors.filter((a) =>
			hits.slice(0, n).some((h) => a.lines.some((line) => covers(h.chunk, a.file, line))),
		).length;

	let firstHitRank = 0;
	for (let i = 0; i < hits.length; i++) {
		if (anchors.some((a) => a.lines.some((line) => covers(hits[i].chunk, a.file, line)))) {
			firstHitRank = i + 1;
			break;
		}
	}
	const total = anchors.length;
	return {
		id: q.id,
		stratum: q.stratum,
		recallAtK: total === 0 ? Number.NaN : hitAt(K) / total,
		recallAt5: total === 0 ? Number.NaN : hitAt(5) / total,
		firstHitRank,
		topScore: hits[0]?.score ?? 0,
		ms,
	};
}

function mean(xs: number[]): number {
	return xs.length === 0 ? Number.NaN : xs.reduce((a, b) => a + b, 0) / xs.length;
}

function report(name: string, results: QuestionResult[]): void {
	const answerable = results.filter((r) => r.stratum !== "negative");
	const negatives = results.filter((r) => r.stratum === "negative");
	const strata = [...new Set(answerable.map((r) => r.stratum))].sort();

	console.log(`\n### ${name}`);
	console.log(`| stratum | n | recall@10 | recall@5 | MRR | mean ms |`);
	console.log(`|---|---:|---:|---:|---:|---:|`);
	for (const s of strata) {
		const rs = answerable.filter((r) => r.stratum === s);
		const mrr = mean(rs.map((r) => (r.firstHitRank === 0 ? 0 : 1 / r.firstHitRank)));
		console.log(
			`| ${s} | ${rs.length} | ${mean(rs.map((r) => r.recallAtK)).toFixed(3)} | ` +
				`${mean(rs.map((r) => r.recallAt5)).toFixed(3)} | ${mrr.toFixed(3)} | ` +
				`${mean(rs.map((r) => r.ms)).toFixed(0)} |`,
		);
	}
	const mrrAll = mean(answerable.map((r) => (r.firstHitRank === 0 ? 0 : 1 / r.firstHitRank)));
	console.log(
		`| **all answerable** | ${answerable.length} | ` +
			`**${mean(answerable.map((r) => r.recallAtK)).toFixed(3)}** | ` +
			`${mean(answerable.map((r) => r.recallAt5)).toFixed(3)} | ${mrrAll.toFixed(3)} | ` +
			`${mean(answerable.map((r) => r.ms)).toFixed(0)} |`,
	);

	// The weighted view Amendment 2 asks for.
	const weighted = answerable.filter((r) => r.stratum === "config" || r.stratum === "cross-file");
	console.log(
		`\nweighted strata (config + cross-file), n=${weighted.length}: ` +
			`recall@10 ${mean(weighted.map((r) => r.recallAtK)).toFixed(3)}`,
	);

	// Not a decision, an indicator: a retrieval score cannot abstain on its own.
	console.log(
		`negatives n=${negatives.length}: mean top-1 score ${mean(negatives.map((r) => r.topScore)).toFixed(2)} ` +
			`vs answerable ${mean(answerable.map((r) => r.topScore)).toFixed(2)}`,
	);

	const misses = answerable.filter((r) => r.recallAtK < 1);
	if (misses.length > 0) {
		console.log(`\npartial or total misses (${misses.length}):`);
		for (const m of misses.slice(0, 25)) {
			console.log(`  ${m.recallAtK.toFixed(2)}  ${m.stratum.padEnd(13)} ${m.id}`);
		}
		if (misses.length > 25) console.log(`  ... and ${misses.length - 25} more`);
	}
}

function main(): void {
	const golden = loadGolden();
	const chunks: Chunk[] = buildChunks();
	const files = corpusFiles();
	console.log(
		`Stage 0 baseline D (lexical) at ${headSha().slice(0, 7)}\n` +
			`${files.length} files, ${chunks.length} chunks, ${golden.length} questions, k=${K}`,
	);

	const locate = chunkLocator(chunks);
	const ftsPlain = buildFtsIndex(chunks, false);
	const ftsUnderscore = buildFtsIndex(chunks, true);

	const runners: [string, (q: string) => Hit[]][] = [
		["D1 FTS5, default tokenizer", (q) => ftsSearch(ftsPlain, q, K)],
		["D1 FTS5, tokenchars '_'", (q) => ftsSearch(ftsUnderscore, q, K)],
		["ripgrep + idf", (q) => ripgrepSearch(files, locate, q, K)],
	];

	for (const [name, run] of runners) {
		const results: QuestionResult[] = [];
		for (const q of golden) {
			const t0 = performance.now();
			const hits = run(q.q);
			results.push(scoreQuestion(q, hits, performance.now() - t0));
		}
		report(name, results);
	}
}

main();
