/**
 * @file Which candidate fix actually moves the config stratum?
 *
 * Four variants over the same golden set, so the Stage 1 build order is chosen
 * by measurement rather than by the order the design brief listed them in.
 *
 *   naive          the committed baseline: 40-line windows
 *   kconfig        grammar-aware chunks for .conf files
 *   expand         naive chunks, query expanded through a vocabulary alias table
 *   kconfig+expand both
 *   headers        naive chunks, each given a deterministic keyword header
 *
 * The alias table and the overfitting hold-out live in `expand.ts`, because the
 * independent-set scorer has to run the same expansion.
 */
import { loadGolden, resolveAnchor } from "./golden.ts";
import { buildChunks, covers, type Chunk } from "./corpus.ts";
import { buildFtsIndex, ftsSearch, ripgrepSearch, chunkLocator, corpusFiles } from "./retrieve.ts";
import { chunkKconfig, isKconfig, readLines } from "./chunk-kconfig.ts";
import { expand, expandHeldOut } from "./expand.ts";
import { withHeaders } from "./headers.ts";

const K = 10;

/** Naive windows everywhere except .conf, which gets one chunk per symbol. */
function hybridChunks(): Chunk[] {
	const out: Chunk[] = [];
	let id = 0;
	for (const c of buildChunks()) {
		if (!isKconfig(c.file)) out.push({ ...c, id: id++ });
	}
	const seen = new Set<string>();
	for (const c of buildChunks()) {
		if (isKconfig(c.file) && !seen.has(c.file)) {
			seen.add(c.file);
			for (const k of chunkKconfig(c.file, readLines(c.file))) out.push({ ...k, id: id++ });
		}
	}
	return out;
}

const mean = (xs: number[]) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Number.NaN);

function main(): void {
	const golden = loadGolden().filter((q) => q.stratum !== "negative");
	const files = corpusFiles();

	const variants: [string, Chunk[], (q: string) => string][] = [
		["naive (baseline)", buildChunks(), (q) => q],
		["kconfig chunker", hybridChunks(), (q) => q],
		["query expansion", buildChunks(), expand],
		["expansion, suspect aliases held out", buildChunks(), expandHeldOut],
		["kconfig + expansion", hybridChunks(), expand],
		["deterministic headers (tier 1)", withHeaders(buildChunks()), (q) => q],
		["headers + query expansion", withHeaders(buildChunks()), expand],
		["kconfig + headers + expansion", withHeaders(hybridChunks()), expand],
	];

	console.log(`stage 1 probe, ${golden.length} answerable questions, k=${K}\n`);
	console.log("| variant | chunks | config@10 | config@5 | config MRR | cross-file@10 | all@10 |");
	console.log("|---|---:|---:|---:|---:|---:|---:|");

	for (const [name, chunks, xf] of variants) {
		const withIds = chunks.map((c, i) => ({ ...c, id: i }));
		const locate = chunkLocator(withIds);
		const fts = buildFtsIndex(withIds, false);

		const per = golden.map((q) => {
			// Reciprocal Rank Fusion over the two channels, so "@5" really means
			// the top 5 of one ranked list. Concatenating two top-10s and slicing
			// silently measured "FTS5's top 10" and called it recall@5.
			const lists = [ftsSearch(fts, xf(q.q), K), ripgrepSearch(files, locate, xf(q.q), K)];
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
			const anchors = q.gold.map(resolveAnchor);
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
			return { stratum: q.stratum, r10: hitAt(K), r5: hitAt(5), mrr: rank ? 1 / rank : 0 };
		});

		const pick = (s: string) => per.filter((p) => p.stratum === s);
		const cfg = pick("config");
		console.log(
			`| ${name} | ${withIds.length} | ${mean(cfg.map((p) => p.r10)).toFixed(3)} | ` +
				`${mean(cfg.map((p) => p.r5)).toFixed(3)} | ${mean(cfg.map((p) => p.mrr)).toFixed(3)} | ` +
				`${mean(pick("cross-file").map((p) => p.r10)).toFixed(3)} | ` +
				`${mean(per.map((p) => p.r10)).toFixed(3)} |`,
		);
	}
}

main();
