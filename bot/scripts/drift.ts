/**
 * @file The drift gate's logic, with no filesystem in it.
 *
 * A stale triage table is worse than no triage table, because people trust it.
 * These checks are what stop src/citations.ts from turning into folklore: if an
 * edit to mk/cdk.mk moves the line a citation points at, CI fails here rather
 * than the bot quietly citing the wrong line at somebody for a year.
 *
 * Kept free of `node:fs` so the tests can drive it with fixtures instead of
 * rewriting the repository.
 */
import type { Citation } from "../src/citations.ts";

export interface Drift {
	file: string;
	line: number;
	expect: string;
	/** The line as it is now, or null if the file or line is gone. */
	actual: string | null;
	reason: "missing-file" | "missing-line" | "substring-moved";
}

/** `read` returns the file's lines, or null if it does not exist. */
export function checkCitations(
	read: (file: string) => string[] | null,
	citations: Citation[],
): Drift[] {
	const drifted: Drift[] = [];

	for (const c of citations) {
		const lines = read(c.file);
		if (lines === null) {
			drifted.push({ ...c, actual: null, reason: "missing-file" });
			continue;
		}
		const actual = lines[c.line - 1];
		if (actual === undefined) {
			drifted.push({ ...c, actual: null, reason: "missing-line" });
			continue;
		}
		if (!actual.includes(c.expect)) {
			drifted.push({ ...c, actual, reason: "substring-moved" });
		}
	}

	return drifted;
}

/**
 * The `paths:` entries of a workflow, flattened across every trigger block.
 *
 * Deliberately a small scanner rather than a YAML parser: the shape it reads is
 * four lines of this repository's own workflow, and a dependency that parses
 * arbitrary YAML is a larger thing to trust than the check is worth.
 */
export function workflowPathPatterns(yaml: string): string[] {
	const patterns: string[] = [];
	let inPaths = false;

	for (const line of yaml.split("\n")) {
		if (/^\s*paths:\s*$/.test(line)) {
			inPaths = true;
			continue;
		}
		if (!inPaths) continue;

		const item = /^\s*-\s+(\S+)\s*$/.exec(line);
		if (item) {
			patterns.push(item[1]!);
		} else if (/^\s*#/.test(line)) {
			// A comment inside the list. Valid YAML, and common here: explain why
			// docs/** is a `**` and not a citation-by-citation list right next to
			// it. Neither an item nor the end of the list.
			continue;
		} else if (line.trim() !== "") {
			inPaths = false;
		}
	}

	return patterns;
}

/**
 * The `options:` list of a `workflow_dispatch` choice input, e.g.
 * `firmware-builds.yml`'s `targets`. Reads every `- item` line between
 * `options:` and the first line at or above its own indentation, skipping
 * comments the same way `workflowPathPatterns` does.
 */
export function choiceInputOptions(yaml: string): string[] {
	const options: string[] = [];
	let inOptions = false;
	let optionsIndent = 0;

	for (const line of yaml.split("\n")) {
		if (!inOptions) {
			const start = /^(\s*)options:\s*$/.exec(line);
			if (start) {
				inOptions = true;
				optionsIndent = start[1]!.length;
			}
			continue;
		}

		const item = /^(\s*)-\s+(\S+)\s*$/.exec(line);
		if (item && item[1]!.length > optionsIndent) {
			options.push(item[2]!);
			continue;
		}
		if (/^\s*#/.test(line)) continue;
		if (line.trim() === "") continue;
		break;
	}

	return options;
}

/**
 * Cited files that no workflow trigger covers.
 *
 * This is the half that makes the gate real. The bot's job only runs when its
 * own trigger matches, so a citation into mk/cdk.mk is unchecked unless
 * mk/cdk.mk is one of the paths that starts the job. Without this, editing the
 * cited line would be exactly the change that does not run the check.
 */
export function uncoveredFiles(patterns: string[], files: string[]): string[] {
	const covers = (pattern: string, file: string): boolean => {
		if (pattern === file) return true;
		if (pattern.endsWith("/**")) return file.startsWith(pattern.slice(0, -2));
		if (pattern.endsWith("**")) return file.startsWith(pattern.slice(0, -2));
		return false;
	};

	return [...new Set(files)].filter((f) => !patterns.some((p) => covers(p, f))).sort();
}
