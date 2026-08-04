/**
 * @file The golden set, and the gate that keeps its labels honest.
 *
 * Gold labels key on an `expect` substring, never on a bare line number. That is
 * not a style choice: the design brief this eval was written against cited
 * `mk/cdk.mk:261-262`, and commit 3737673 grew that file by 71 lines and moved
 * every one of those anchors a uniform +20. A golden set keyed on line numbers
 * would have silently measured every baseline against the wrong lines.
 *
 * Same mechanism as scripts/check-citations.ts, one level stricter: an `expect`
 * that matches more than one line in its file is reported too, because an
 * ambiguous anchor makes recall look better than it is.
 */
import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

export const REPO_ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

export type Stratum =
	| "config"
	| "identifier"
	| "error-string"
	| "cross-file"
	| "conceptual"
	| "negative";

export interface GoldAnchor {
	file: string;
	expect: string;
}

export interface GoldenQuestion {
	id: string;
	stratum: Stratum;
	q: string;
	gold: GoldAnchor[];
}

/** Where an anchor actually resolved, at the tree as it stands right now. */
export interface ResolvedAnchor extends GoldAnchor {
	/** 1-indexed lines whose text contains `expect`. Empty means the gate fails. */
	lines: number[];
}

const fileCache = new Map<string, string[]>();

function lines(file: string): string[] {
	let cached = fileCache.get(file);
	if (!cached) {
		cached = readFileSync(join(REPO_ROOT, file), "utf8").split("\n");
		fileCache.set(file, cached);
	}
	return cached;
}

/** Every 1-indexed line in `file` containing `expect`. */
export function resolveAnchor(anchor: GoldAnchor): ResolvedAnchor {
	const hits: number[] = [];
	lines(anchor.file).forEach((text, i) => {
		if (text.includes(anchor.expect)) hits.push(i + 1);
	});
	return { ...anchor, lines: hits };
}

export function loadGolden(): GoldenQuestion[] {
	const path = join(REPO_ROOT, "bot", "eval", "golden.jsonl");
	return readFileSync(path, "utf8")
		.split("\n")
		.filter((l) => l.trim().length > 0)
		.map((l) => JSON.parse(l) as GoldenQuestion);
}

/** The SHA the whole eval is pinned to, for the report header. Shelled out
 *  rather than read from .git/HEAD, because in a git worktree .git is a file. */
export function headSha(): string {
	return execFileSync("git", ["rev-parse", "HEAD"], { cwd: REPO_ROOT, encoding: "utf8" }).trim();
}

function main(): void {
	const golden = loadGolden();
	const byStratum = new Map<string, number>();
	const missing: string[] = [];
	const ambiguous: string[] = [];
	let anchors = 0;

	for (const q of golden) {
		byStratum.set(q.stratum, (byStratum.get(q.stratum) ?? 0) + 1);
		if (q.stratum === "negative") {
			if (q.gold.length > 0) missing.push(`${q.id}: negative question must have no gold`);
			continue;
		}
		if (q.gold.length === 0) {
			missing.push(`${q.id}: non-negative question has no gold anchor`);
			continue;
		}
		for (const anchor of q.gold) {
			anchors++;
			const resolved = resolveAnchor(anchor);
			if (resolved.lines.length === 0) {
				missing.push(`${q.id}: ${anchor.file} has no line containing "${anchor.expect}"`);
			} else if (resolved.lines.length > 1) {
				ambiguous.push(
					`${q.id}: ${anchor.file} "${anchor.expect}" matches ${resolved.lines.length} lines (${resolved.lines.join(", ")})`,
				);
			}
		}
	}

	console.log(`golden set: ${golden.length} questions, ${anchors} anchors, at ${headSha().slice(0, 7)}`);
	for (const [stratum, n] of [...byStratum].sort()) console.log(`  ${stratum.padEnd(13)} ${n}`);

	if (ambiguous.length > 0) {
		console.log(`\n${ambiguous.length} ambiguous anchor(s) — tighten these:`);
		for (const a of ambiguous) console.log(`  ! ${a}`);
	}
	if (missing.length > 0) {
		console.error(`\n${missing.length} BROKEN anchor(s):`);
		for (const m of missing) console.error(`  x ${m}`);
		process.exit(1);
	}
	console.log(`\nAll ${anchors} anchors resolve.`);
}

if (process.argv[1] && import.meta.url.endsWith(process.argv[1].split("/").pop() ?? "")) main();
