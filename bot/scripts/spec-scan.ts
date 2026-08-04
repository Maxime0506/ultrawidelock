/**
 * @file Find every Aliro 1.0 section reference in docs/.
 *
 * `/spec <section>` answers "which files cite this" without ever printing
 * spec text: the spec itself (`internal/aliro-1.0.txt`) is gitignored and
 * this scanner never opens it. It reads only the tracked `docs/*.md` prose
 * that already cites the spec, the same way a person grepping the tree would.
 *
 * Two shapes of citation, both observed in the tree:
 *
 *   1. Inline: a line mentioning "Aliro 1.0" that also carries a `§N`,
 *      `section N` or `Table N-N` token, e.g. docs/power-profile.md:8.
 *   2. Tabular: a markdown table whose header row names "Aliro 1.0" as a
 *      column (docs/esp32-gotchas.md's "Spec, per fact" table) — every row
 *      under it is a citation even though the row itself never repeats the
 *      words "Aliro 1.0".
 *
 * This is a heuristic over prose, not a parser with a grammar, so it is
 * re-run by a test against the live tree rather than trusted once and
 * forgotten. That test is this file's own drift gate.
 */
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

export interface SpecCitation {
	/** e.g. "14", "11.3.1", "Table 8-3". Never a page or a quote. */
	section: string;
	file: string;
	line: number;
}

const TOKEN = /§\s*(\d+(?:\.\d+)*)|\bsection\s+(\d+(?:\.\d+)*)\b|\bTable\s+(\d+-\d+)/gi;

/** The bare "(Aliro 1.0, 11.7.3.4.1)" form: a dotted number directly after
 *  the words "Aliro 1.0", with no §, "section" or "Table" to mark it. Needs
 *  at least one dot, or every plain "Aliro 1.0" would self-match as "1.0". */
const AFTER_ALIRO = /\bAliro 1\.0,\s*(\d+(?:\.\d+){2,})/gi;

function tokensOn(line: string): string[] {
	const found: string[] = [];
	for (const m of line.matchAll(TOKEN)) {
		found.push(m[1] ?? m[2] ?? `Table ${m[3]}`);
	}
	for (const m of line.matchAll(AFTER_ALIRO)) {
		found.push(m[1]!);
	}
	return found;
}

/** `read` returns a docs/*.md file's contents, or null. Exposed for tests, so
 *  they can drive this against fixtures without touching the filesystem. */
export function scanText(file: string, text: string): SpecCitation[] {
	const lines = text.split("\n");
	const citations: SpecCitation[] = [];
	let inSpecTable = false;

	for (let i = 0; i < lines.length; i++) {
		const line = lines[i]!;
		const isTableRow = /^\s*\|/.test(line);

		if (isTableRow && /\bAliro 1\.0\b/.test(line)) {
			// The header row itself, e.g. "| Fact | Aliro 1.0 |". Not a citation.
			inSpecTable = true;
			continue;
		}
		if (isTableRow && /^\s*\|[\s-:|]+\|\s*$/.test(line)) {
			// The `| --- | --- |` separator row. Neither a citation nor an exit.
			continue;
		}
		if (!isTableRow) {
			inSpecTable = false;
		}

		if (isTableRow && inSpecTable) {
			for (const section of tokensOn(line)) {
				citations.push({ section, file, line: i + 1 });
			}
			continue;
		}

		if (/\bAliro 1\.0\b/.test(line)) {
			for (const section of tokensOn(line)) {
				citations.push({ section, file, line: i + 1 });
			}
		}
	}

	return citations;
}

/** Every `docs/*.md` file, scanned. `docsDir` is `docs/` itself so tests can
 *  point it at a fixture directory instead. */
export function scanDocs(docsDir: string, repoRelativePrefix: string): SpecCitation[] {
	const citations: SpecCitation[] = [];
	for (const name of readdirSync(docsDir).sort()) {
		if (!name.endsWith(".md")) continue;
		const text = readFileSync(join(docsDir, name), "utf8");
		citations.push(...scanText(`${repoRelativePrefix}${name}`, text));
	}
	return citations;
}
