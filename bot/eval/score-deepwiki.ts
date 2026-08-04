/**
 * @file Re-score the cached DeepWiki answers, with a metric that is fair to prose.
 *
 * The strict metric in deepwiki.ts asks whether the answer contains the gold
 * `expect` text verbatim. For a config anchor that is nearly fair, but it scored
 * this as a miss:
 *
 *   expect: CONFIG_BT_MAX_CONN=1
 *   answer: "...for the DWM3001CDK target, this value is set to `1`"
 *
 * which is a correct answer. Reporting 0.020 on the identifier stratum off the
 * back of that would have been a measurement artifact presented as a finding.
 * So two metrics are reported side by side:
 *
 *   strict — the gold text appears verbatim. A floor.
 *   fair   — for `SYMBOL=VALUE` anchors, the answer names SYMBOL and states
 *            VALUE within 120 characters of it. For prose anchors, at least 80%
 *            of the gold line's distinctive tokens appear.
 *
 * `fair` is the honest read of whether the answer carries the fact. `strict`
 * stays because it is the one a machine could check without judgement, and the
 * gap between them is itself informative.
 */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { loadGolden, resolveAnchor, REPO_ROOT, type GoldAnchor } from "./golden.ts";

const CACHE = join(REPO_ROOT, "bot", "eval", ".deepwiki-cache.json");

const norm = (s: string) =>
	s
		.toLowerCase()
		.replace(/[`'"*]/g, "")
		.replace(/\s+/g, " ")
		.trim();

const STOP = new Set(["the", "and", "for", "not", "その", "at", "on", "is", "to", "a", "an", "of", "in"]);

/** Distinctive tokens of a gold line: alphanumeric/underscore runs of 3+ chars. */
function tokens(expect: string): string[] {
	return (norm(expect).match(/[a-z0-9_]{3,}/g) ?? []).filter((t) => !STOP.has(t));
}

/** `CONFIG_X=value` -> ["config_x", "value"], else null. */
function symbolValue(expect: string): [string, string] | null {
	const m = expect.match(/^([A-Z][A-Z0-9_]*)=(.+)$/);
	if (!m) return null;
	return [norm(m[1]), norm(m[2])];
}

function strictHit(answer: string, a: GoldAnchor): boolean {
	return norm(answer).includes(norm(a.expect));
}

/**
 * Does `window` assert `value` for a Kconfig symbol?
 *
 * Kconfig's `y` and `n` are one character long, so a substring test scores
 * "is enabled" as an assertion of `=n` — the "n" inside "enabled". That
 * over-credited most of the config stratum before it was caught. Booleans are
 * matched as whole words or by their prose equivalents; everything else needs a
 * word-boundary match so `=1` is not satisfied by the 1 in 1024.
 */
function assertsValue(window: string, value: string): boolean {
	const word = (v: string) => new RegExp(`(?<![a-z0-9_])${v.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}(?![a-z0-9_])`).test(window);
	if (value === "y") return word("y") || /\b(enabled|turned on|is on|set to y|true)\b/.test(window);
	if (value === "n") return word("n") || /\b(disabled|turned off|is off|not enabled|set to n|false)\b/.test(window);
	return word(value);
}

function fairHit(answer: string, a: GoldAnchor): boolean {
	const n = norm(answer);
	const sv = symbolValue(a.expect);
	if (sv) {
		const [sym, val] = sv;
		// The symbol must be named, and the value asserted near it. Proximity
		// matters: prj.conf mentions dozens of symbols and dozens of numbers, so
		// "contains both somewhere" would pass on almost any answer about the file.
		let from = 0;
		for (;;) {
			const i = n.indexOf(sym, from);
			if (i < 0) return false;
			if (assertsValue(n.slice(i, i + sym.length + 120), val)) return true;
			from = i + 1;
		}
	}
	const ts = tokens(a.expect);
	if (ts.length === 0) return false;
	return ts.filter((t) => n.includes(t)).length / ts.length >= 0.8;
}

const mean = (xs: number[]) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : Number.NaN);

function main(): void {
	const cache = JSON.parse(readFileSync(CACHE, "utf8")) as Record<string, { text: string; ms: number }>;
	const golden = loadGolden().filter((q) => cache[q.id]);

	const rows = golden
		.filter((q) => q.stratum !== "negative")
		.map((q) => {
			const anchors = q.gold.map(resolveAnchor);
			const text = cache[q.id].text;
			const frac = (f: (a: GoldAnchor) => boolean) => anchors.filter(f).length / anchors.length;
			return {
				id: q.id,
				stratum: q.stratum,
				strict: frac((a) => strictHit(text, a)),
				fair: frac((a) => fairHit(text, a)),
				ms: cache[q.id].ms,
			};
		});

	console.log("### Baseline A: DeepWiki, strict vs fair\n");
	console.log("| stratum | n | strict | fair | mean s |");
	console.log("|---|---:|---:|---:|---:|");
	for (const s of [...new Set(rows.map((r) => r.stratum))].sort()) {
		const rs = rows.filter((r) => r.stratum === s);
		console.log(
			`| ${s} | ${rs.length} | ${mean(rs.map((r) => r.strict)).toFixed(3)} | ` +
				`**${mean(rs.map((r) => r.fair)).toFixed(3)}** | ${(mean(rs.map((r) => r.ms)) / 1000).toFixed(1)} |`,
		);
	}
	console.log(
		`| **all answerable** | ${rows.length} | ${mean(rows.map((r) => r.strict)).toFixed(3)} | ` +
			`**${mean(rows.map((r) => r.fair)).toFixed(3)}** | ${(mean(rows.map((r) => r.ms)) / 1000).toFixed(1)} |`,
	);

	const w = rows.filter((r) => r.stratum === "config" || r.stratum === "cross-file");
	console.log(
		`\nweighted strata (config + cross-file), n=${w.length}: ` +
			`strict ${mean(w.map((r) => r.strict)).toFixed(3)}  fair ${mean(w.map((r) => r.fair)).toFixed(3)}`,
	);
}

main();
