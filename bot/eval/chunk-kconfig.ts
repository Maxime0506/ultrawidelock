/**
 * @file A grammar-aware chunker for Kconfig fragments, and the experiment that
 *       says whether it earns its place.
 *
 * The prediction being tested is that it will NOT fix the config stratum.
 * `firmware/prj.conf:226` fails today because the question says "serial port"
 * and the file says "console", "RTT" and "UART" — and the naive 40-line window
 * already contains that comment. Attaching the comment more precisely does not
 * add a word the file never uses.
 *
 * What it should improve is precision: one chunk per CONFIG symbol, carrying its
 * own comment and its section header and nothing else, means a hit is the fact
 * rather than a 40-line neighbourhood that happens to contain it. That shows up
 * in MRR and recall@5 rather than recall@10.
 *
 * Writing the prediction down first so the measurement can contradict it.
 */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { REPO_ROOT } from "./golden.ts";
import type { Chunk } from "./corpus.ts";

/** `# ---- what is deliberately absent ----` and friends. */
const SECTION = /^#\s*-{2,}\s*(.+?)\s*-{2,}\s*$/;
const ASSIGN = /^(CONFIG_[A-Za-z0-9_]+)\s*=/;

/**
 * One chunk per `CONFIG_*` assignment: the assignment, the unbroken comment
 * block directly above it, and the most recent section header.
 *
 * Consecutive assignments with no comment between them are kept together, since
 * a run like the PSA_WANT_* block is one decision written as eight lines.
 */
export function chunkKconfig(file: string, lines: string[]): Chunk[] {
	const chunks: Chunk[] = [];
	let section = "";
	let i = 0;

	while (i < lines.length) {
		const secMatch = lines[i].match(SECTION);
		if (secMatch) {
			section = secMatch[1];
			i++;
			continue;
		}

		// Gather a comment block, then whatever assignments it introduces.
		const commentStart = i;
		while (i < lines.length && /^#/.test(lines[i]) && !SECTION.test(lines[i])) i++;
		const comment = lines.slice(commentStart, i);

		const assignStart = i;
		while (i < lines.length && (ASSIGN.test(lines[i]) || lines[i].trim() === "")) {
			if (lines[i].trim() === "" && i > assignStart && !ASSIGN.test(lines[i - 1] ?? "")) break;
			i++;
		}
		const assigns = lines.slice(assignStart, i);

		if (assigns.some((l) => ASSIGN.test(l))) {
			const start = comment.length > 0 ? commentStart + 1 : assignStart + 1;
			const end = i;
			const header = section ? `# section: ${section}` : "";
			chunks.push({
				id: 0,
				file,
				start,
				end,
				text: [header, ...comment, ...assigns].filter(Boolean).join("\n"),
			});
		}

		if (i === commentStart) i++; // no progress: a blank or unrecognised line
	}
	return chunks;
}

export function isKconfig(file: string): boolean {
	return /\.conf$/.test(file);
}

export function readLines(file: string): string[] {
	return readFileSync(join(REPO_ROOT, file), "utf8").split("\n");
}
