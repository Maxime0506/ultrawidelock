/**
 * @file Baseline A: DeepWiki, scored against the same golden set.
 *
 * DeepWiki answers in prose rather than returning chunks, so recall@k has no
 * meaning here and pretending otherwise would make the two baselines look
 * comparable when they are not. Three things are measured instead:
 *
 *   fact  — the answer contains the gold `expect` text itself
 *   file  — the answer names the file the fact lives in
 *   cite  — the answer gives a `path:line` that RESOLVES to the gold line at
 *           the pinned SHA, which is Amendment 2's third weighted criterion
 *
 * `fact` is strict and understates a prose answer that paraphrases a doc
 * comment correctly, so `file` is reported beside it rather than instead of it.
 * For a config or identifier anchor, where the gold text is a literal token a
 * correct answer has to print, `fact` is the honest measure.
 *
 * Responses are cached so scoring can be re-run without re-querying a free
 * service.
 */
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { loadGolden, resolveAnchor, headSha, REPO_ROOT, type GoldenQuestion } from "./golden.ts";

const ENDPOINT = "https://mcp.deepwiki.com/mcp";
const REPO = "openaliro/openaliro";
const CACHE = join(REPO_ROOT, "bot", "eval", ".deepwiki-cache.json");
const CONCURRENCY = 4;

interface Answer {
	text: string;
	ms: number;
}

type Cache = Record<string, Answer>;

function loadCache(): Cache {
	return existsSync(CACHE) ? (JSON.parse(readFileSync(CACHE, "utf8")) as Cache) : {};
}

/** One `tools/call`, over the same streamable-HTTP endpoint the MCP client uses. */
async function ask(question: string): Promise<Answer> {
	const t0 = performance.now();
	const res = await fetch(ENDPOINT, {
		method: "POST",
		headers: { "content-type": "application/json", accept: "application/json, text/event-stream" },
		body: JSON.stringify({
			jsonrpc: "2.0",
			id: 1,
			method: "tools/call",
			params: { name: "ask_question", arguments: { repoName: REPO, question } },
		}),
	});
	const body = await res.text();
	let text = "";
	for (const line of body.split("\n")) {
		if (!line.startsWith("data: ")) continue;
		try {
			const j = JSON.parse(line.slice(6));
			text = j?.result?.content?.[0]?.text ?? text;
		} catch {
			/* SSE keepalives and partial frames are not answers */
		}
	}
	return { text, ms: performance.now() - t0 };
}

const norm = (s: string) =>
	s
		.toLowerCase()
		.replace(/[`'"]/g, "")
		.replace(/\s+/g, " ")
		.trim();

/** Every `path:line` in the answer that actually resolves to `expect` at HEAD. */
function citesGoldLine(answer: string, file: string, goldLines: number[]): boolean {
	const base = file.split("/").pop() ?? file;
	const re = new RegExp(`(?:${file.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}|${base.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")})[:#]L?(\\d+)`, "g");
	for (const m of answer.matchAll(re)) {
		if (goldLines.includes(Number(m[1]))) return true;
	}
	return false;
}

const REFUSAL =
	/\b(no information|not (?:documented|found|available|present|covered|mention)|does not (?:contain|document|mention|cover|specify)|cannot (?:find|answer|determine)|unable to|outside the scope|not (?:in|part of) (?:this|the) repos|no (?:mention|reference|documentation))\b/i;

interface Row {
	id: string;
	stratum: string;
	fact: number;
	file: number;
	cite: number;
	refused: boolean;
	ms: number;
}

function score(q: GoldenQuestion, a: Answer): Row {
	const n = norm(a.text);
	const anchors = q.gold.map(resolveAnchor);
	const hits = (f: (x: ReturnType<typeof resolveAnchor>) => boolean) =>
		anchors.length === 0 ? Number.NaN : anchors.filter(f).length / anchors.length;

	return {
		id: q.id,
		stratum: q.stratum,
		fact: hits((x) => n.includes(norm(x.expect))),
		file: hits((x) => n.includes(norm(x.file)) || n.includes(norm(x.file.split("/").pop() ?? ""))),
		cite: hits((x) => citesGoldLine(a.text, x.file, x.lines)),
		refused: REFUSAL.test(a.text),
		ms: a.ms,
	};
}

const mean = (xs: number[]) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Number.NaN);

async function main(): Promise<void> {
	const golden = loadGolden();
	const cache = loadCache();
	const todo = golden.filter((q) => !cache[q.id]);
	console.log(
		`Baseline A (DeepWiki) at ${headSha().slice(0, 7)}: ${golden.length} questions, ` +
			`${todo.length} to fetch, ${golden.length - todo.length} cached`,
	);

	let done = 0;
	const queue = [...todo];
	await Promise.all(
		Array.from({ length: CONCURRENCY }, async () => {
			for (let q = queue.shift(); q; q = queue.shift()) {
				try {
					cache[q.id] = await ask(q.q);
				} catch (e) {
					cache[q.id] = { text: `ERROR: ${(e as Error).message}`, ms: 0 };
				}
				if (++done % 10 === 0) {
					writeFileSync(CACHE, JSON.stringify(cache, null, 1));
					console.log(`  ${done}/${todo.length}`);
				}
			}
		}),
	);
	writeFileSync(CACHE, JSON.stringify(cache, null, 1));

	const rows = golden.map((q) => score(q, cache[q.id]));
	const answerable = rows.filter((r) => r.stratum !== "negative");
	const negatives = rows.filter((r) => r.stratum === "negative");

	console.log(`\n### Baseline A: DeepWiki ask_question`);
	console.log(`| stratum | n | fact | file | cite | mean ms |`);
	console.log(`|---|---:|---:|---:|---:|---:|`);
	for (const s of [...new Set(answerable.map((r) => r.stratum))].sort()) {
		const rs = answerable.filter((r) => r.stratum === s);
		console.log(
			`| ${s} | ${rs.length} | ${mean(rs.map((r) => r.fact)).toFixed(3)} | ` +
				`${mean(rs.map((r) => r.file)).toFixed(3)} | ${mean(rs.map((r) => r.cite)).toFixed(3)} | ` +
				`${mean(rs.map((r) => r.ms)).toFixed(0)} |`,
		);
	}
	console.log(
		`| **all answerable** | ${answerable.length} | ` +
			`**${mean(answerable.map((r) => r.fact)).toFixed(3)}** | ` +
			`${mean(answerable.map((r) => r.file)).toFixed(3)} | ` +
			`**${mean(answerable.map((r) => r.cite)).toFixed(3)}** | ` +
			`${mean(answerable.map((r) => r.ms)).toFixed(0)} |`,
	);

	const weighted = answerable.filter((r) => r.stratum === "config" || r.stratum === "cross-file");
	console.log(
		`\nweighted strata (config + cross-file), n=${weighted.length}: ` +
			`fact ${mean(weighted.map((r) => r.fact)).toFixed(3)}  ` +
			`file ${mean(weighted.map((r) => r.file)).toFixed(3)}  ` +
			`cite ${mean(weighted.map((r) => r.cite)).toFixed(3)}`,
	);
	console.log(
		`negatives n=${negatives.length}: refused cleanly ${negatives.filter((r) => r.refused).length}/${negatives.length}`,
	);
	for (const n of negatives) {
		console.log(`  ${n.refused ? "refused " : "ANSWERED"}  ${n.id}`);
	}
}

await main();
