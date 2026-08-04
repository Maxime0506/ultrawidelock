/**
 * @file What gets indexed, and how it is cut up for the Stage 0 baseline.
 *
 * The chunker here is deliberately naive: fixed line windows with overlap, no
 * grammar awareness at all. That is the point. Stage 0 measures the floor, so
 * the custom Kconfig/devicetree/Makefile chunkers proposed for Stage 1 have a
 * number to beat rather than an assertion to agree with.
 *
 * File selection is `git ls-files` minus binaries and minus anything generated,
 * so a hit can never come from a file that would be rebuilt rather than edited.
 */
import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { REPO_ROOT } from "./golden.ts";
import { join } from "node:path";

/** Windows are 40 lines with 10 of overlap: big enough to carry a CONFIG line
 *  and the comment block above it, small enough that a hit is still specific. */
export const WINDOW = 40;
export const OVERLAP = 10;

const SKIP_EXT =
	/\.(gif|png|jpe?g|ico|svg|ttf|otf|woff2?|wasm|frc|hex|bin|elf|pdf|zip|gz|db|map)$/i;

/** Generated, never hand-edited: a citation into one of these is a citation
 *  into a build artifact, which is exactly what the bot must not hand a user. */
const SKIP_PATH = [
	/(^|\/)node_modules\//,
	/(^|\/)package-lock\.json$/,
	/\.generated\.ts$/,
	/^web-twin\/twin\.js$/,
	/^docs\/ARCHITECTURE\.md$/,
	/^docs\/architecture\//,
	/^site\//,
	// The eval itself. golden.jsonl is the answer key: once bot/eval was
	// committed it became a tracked file, and indexing it put every question
	// next to the exact text of its own gold anchor. Leaving it in did not
	// inflate the score -- those chunks match a query strongly, then fail the
	// hit test because they are the wrong file, so they crowd real answers out
	// of the top 10 and recall FELL. Either direction is a corrupt measurement.
	/^bot\/eval\//,
];

export interface Chunk {
	id: number;
	file: string;
	/** 1-indexed, inclusive. */
	start: number;
	end: number;
	text: string;
}

export function indexableFiles(): string[] {
	return execFileSync("git", ["ls-files"], {
		cwd: REPO_ROOT,
		encoding: "utf8",
		maxBuffer: 64 * 1024 * 1024,
	})
		.split("\n")
		.filter((f) => f.length > 0)
		.filter((f) => !SKIP_EXT.test(f))
		.filter((f) => !SKIP_PATH.some((re) => re.test(f)));
}

/** Read a file as lines, or null when it is not valid UTF-8 text. */
function readLines(file: string): string[] | null {
	let raw: Buffer;
	try {
		raw = readFileSync(join(REPO_ROOT, file));
	} catch {
		return null;
	}
	if (raw.includes(0)) return null; // NUL byte: binary that slipped the extension filter
	return raw.toString("utf8").split("\n");
}

export function buildChunks(): Chunk[] {
	const chunks: Chunk[] = [];
	let id = 0;
	for (const file of indexableFiles()) {
		const lines = readLines(file);
		if (!lines) continue;
		const step = WINDOW - OVERLAP;
		for (let start = 0; start < lines.length; start += step) {
			const end = Math.min(start + WINDOW, lines.length);
			const text = lines.slice(start, end).join("\n");
			if (text.trim().length > 0) {
				chunks.push({ id: id++, file, start: start + 1, end, text });
			}
			if (end >= lines.length) break;
		}
	}
	return chunks;
}

/** True when `chunk` covers `line` of `file` — the hit test the scorer uses. */
export function covers(chunk: Chunk, file: string, line: number): boolean {
	return chunk.file === file && line >= chunk.start && line <= chunk.end;
}
