/**
 * @file The drift gate.
 *
 *   npm run drift
 *
 * Fails when a line cited by src/citations.ts no longer says what the table
 * claims it says, or when a cited file is not one of the paths that starts the
 * bot workflow. Both are run by .github/workflows/bot.yml.
 *
 * Exit 0 clean, 1 on a finding.
 */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { allCitations } from "../src/citations.ts";
import { checkCitations, uncoveredFiles, workflowPathPatterns } from "./drift.ts";

const REPO_ROOT = join(import.meta.dirname, "../..");
const WORKFLOW = join(REPO_ROOT, ".github/workflows/bot.yml");

function readLines(file: string): string[] | null {
	try {
		return readFileSync(join(REPO_ROOT, file), "utf8").split("\n");
	} catch {
		return null;
	}
}

const citations = allCitations();
let failed = false;

const drifted = checkCitations(readLines, citations);
if (drifted.length > 0) {
	failed = true;
	console.error(`\n${drifted.length} citation(s) no longer point at what they claim:\n`);
	for (const d of drifted) {
		console.error(`  ${d.file}:${d.line}  (${d.reason})`);
		console.error(`    expected to contain: ${JSON.stringify(d.expect)}`);
		console.error(`    line now says:       ${d.actual === null ? "(gone)" : JSON.stringify(d.actual.trim())}`);
	}
	console.error(
		`\nFix the line number in bot/src/citations.ts, or drop the entry. Do not\n` +
			`loosen the expected substring to make this pass: it is the only thing\n` +
			`standing between the triage table and folklore.\n`,
	);
}

const uncovered = uncoveredFiles(
	workflowPathPatterns(readFileSync(WORKFLOW, "utf8")),
	citations.map((c) => c.file),
);
if (uncovered.length > 0) {
	failed = true;
	console.error(`\n${uncovered.length} cited file(s) do not trigger this workflow:\n`);
	for (const f of uncovered) console.error(`  ${f}`);
	console.error(
		`\nAdd them to the \`paths:\` lists in .github/workflows/bot.yml. Until then\n` +
			`an edit to one of those files is exactly the change that skips this gate.\n`,
	);
}

if (failed) process.exit(1);

console.log(
	`${citations.length} citations check out across ${new Set(citations.map((c) => c.file)).size} files.`,
);
