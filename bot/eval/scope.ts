/**
 * @file Does narrowing what gets indexed improve retrieval?
 *
 * Prompted by a repomix run: excluding tests and markdown shrinks this tree a
 * long way, which raises the question of whether the index is carrying weight
 * that only adds noise. Two scopes are worth testing separately.
 *
 * The vendored Qorvo driver (deps/dw3000/dwt_uwb_driver) is 1.43 M chars, 15% of
 * the corpus, and it is LicenseRef-QORVO-2 — use tied to a Qorvo IC, and
 * reverse-engineering prohibited. There is a compliance argument for keeping it
 * out of an index that feeds answers into a public channel, independent of
 * whether it helps or hurts recall. This measures the retrieval half so the
 * decision can be made on both.
 *
 * Markdown is deliberately NOT dropped: docs/troubleshooting.md and
 * dwm3001cdk-surgery.md hold the entire error-string stratum.
 */
import { loadGolden, resolveAnchor } from "./golden.ts";
import { buildChunks, covers, type Chunk } from "./corpus.ts";
import { buildFtsIndex, ftsSearch, ripgrepSearch, chunkLocator, corpusFiles } from "./retrieve.ts";

const K = 10;

const isTest = (f: string) => /(^|\/)(tests?|__tests__)\//.test(f) || /\.test\.|_test\.|test_/.test(f);
const isVendor = (f: string) => /^deps\/dw3000\/dwt_uwb_driver\//.test(f);

const SCOPES: [string, (f: string) => boolean][] = [
	["everything (baseline D)", () => true],
	["minus vendored Qorvo driver", (f) => !isVendor(f)],
	["minus tests", (f) => !isTest(f)],
	["minus both", (f) => !isVendor(f) && !isTest(f)],
];

const mean = (xs: number[]) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Number.NaN);

function main(): void {
	const golden = loadGolden().filter((q) => q.stratum !== "negative");
	const allChunks = buildChunks();
	const allFiles = corpusFiles();

	console.log(`scope experiment, ${golden.length} answerable questions, k=${K}\n`);
	console.log("| scope | files | chunks | rg recall@10 | FTS5 recall@10 | weighted (rg) |");
	console.log("|---|---:|---:|---:|---:|---:|");

	for (const [name, keep] of SCOPES) {
		const chunks: Chunk[] = allChunks.filter((c) => keep(c.file));
		const files = allFiles.filter(keep);
		const locate = chunkLocator(chunks);
		const fts = buildFtsIndex(chunks, false);

		const score = (hits: { chunk: Chunk }[], q: (typeof golden)[number]) => {
			const anchors = q.gold.map(resolveAnchor);
			return (
				anchors.filter((a) =>
					hits.some((h) => a.lines.some((line) => covers(h.chunk, a.file, line))),
				).length / anchors.length
			);
		};

		const rg = golden.map((q) => score(ripgrepSearch(files, locate, q.q, K), q));
		const f5 = golden.map((q) => score(ftsSearch(fts, q.q, K), q));
		const wIdx = golden
			.map((q, i) => (q.stratum === "config" || q.stratum === "cross-file" ? rg[i] : Number.NaN))
			.filter((x) => !Number.isNaN(x));

		console.log(
			`| ${name} | ${files.length} | ${chunks.length} | ${mean(rg).toFixed(3)} | ` +
				`${mean(f5).toFixed(3)} | ${mean(wIdx).toFixed(3)} |`,
		);
	}
}

main();
