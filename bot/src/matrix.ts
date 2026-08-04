/**
 * @file The compatibility matrix: pure formatting, no D1 and no Discord
 * wire types, so it is testable without either.
 *
 * Only two of the four documented glyphs are reachable yet. ✅ (validated)
 * and ❌ (known-broken) both come from test *results*, which is the
 * `/test-request` + `/test-result` machinery — not built. Showing them here
 * would be a status this bot has not actually observed, so for now every
 * cell is either ⚠️ (someone owns that board/iOS pair) or ❓ (nobody does).
 * `matrixTable` takes an optional results map so this file does not need to
 * change again once that phase lands.
 */
import { BOARDS, boardLabel } from "./boards.ts";
import type { MatrixCount } from "./rigs.ts";

export type Glyph = "✅" | "⚠️" | "❌" | "❓";

/** Compare dot-separated numeric version strings ("9.1" < "19.1" < "19.1.2"),
 *  not lexicographically ("19.1" would otherwise sort before "9.1"). Missing
 *  trailing segments compare as 0, so "19.1" < "19.1.2". */
export function compareVersions(a: string, b: string): number {
	const as = a.split(".").map(Number);
	const bs = b.split(".").map(Number);
	const len = Math.max(as.length, bs.length);
	for (let i = 0; i < len; i++) {
		const diff = (as[i] ?? 0) - (bs[i] ?? 0);
		if (diff !== 0) return diff;
	}
	return 0;
}

/** Every iOS version with at least one registered owner, ascending. */
export function distinctVersions(counts: readonly MatrixCount[]): string[] {
	return [...new Set(counts.map((c) => c.ios_version))].sort(compareVersions);
}

/** validated/known-broken results are not modeled yet (see file header);
 *  this shape exists so callers and tests have somewhere to put them once
 *  `/test-result` starts producing them. */
export interface ValidationStatus {
	board: string;
	iosVersion: string;
	passed: boolean;
}

function glyphFor(owners: number, status?: ValidationStatus): { glyph: Glyph; owners: number } {
	if (status) return { glyph: status.passed ? "✅" : "❌", owners };
	return owners > 0 ? { glyph: "⚠️", owners } : { glyph: "❓", owners: 0 };
}

/** A monospace grid: rows are every known board (in the order boards.ts
 *  lists them, even ones with zero owners — an all-❓ row is the signal
 *  this bot exists to surface), columns are every iOS version seen in the
 *  registry, ascending. Returns null if the registry has no iOS versions
 *  recorded at all, since there is then no column axis to draw. */
export function matrixTable(
	counts: readonly MatrixCount[],
	results: readonly ValidationStatus[] = [],
): string | null {
	const versions = distinctVersions(counts);
	if (versions.length === 0) return null;

	const countFor = (board: string, v: string): number =>
		counts.find((c) => c.board === board && c.ios_version === v)?.n ?? 0;
	const resultFor = (board: string, v: string): ValidationStatus | undefined =>
		results.find((r) => r.board === board && r.iosVersion === v);

	const boardHeader = "board";
	const boardWidth = Math.max(boardHeader.length, ...BOARDS.map((b) => boardLabel(b.value).length));
	const cellText = (board: string, v: string): string => {
		const { glyph, owners } = glyphFor(countFor(board, v), resultFor(board, v));
		return `${owners}${glyph}`;
	};
	const colWidth = (v: string): number =>
		Math.max(v.length, ...BOARDS.map((b) => cellText(b.value, v).length));
	const widths = versions.map(colWidth);

	const pad = (s: string, w: number): string => s + " ".repeat(Math.max(0, w - s.length));

	const lines: string[] = [];
	lines.push(
		`${pad(boardHeader, boardWidth)} | ${versions.map((v, i) => pad(v, widths[i]!)).join(" | ")}`,
	);
	lines.push(`${"-".repeat(boardWidth)}-+-${widths.map((w) => "-".repeat(w)).join("-+-")}`);
	for (const b of BOARDS) {
		const row = versions.map((v, i) => pad(cellText(b.value, v), widths[i]!));
		lines.push(`${pad(boardLabel(b.value), boardWidth)} | ${row.join(" | ")}`);
	}
	return lines.join("\n");
}
