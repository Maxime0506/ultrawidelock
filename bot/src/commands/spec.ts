/**
 * @file `/spec <section>` — which files in this repository cite an Aliro 1.0
 * section. Pointers only.
 *
 * The spec text itself (`internal/aliro-1.0.txt`) is gitignored and this bot
 * never reads it; `SPEC_CITATIONS` is built by scanning the tracked prose
 * that already cites the spec, not the spec. Answering with anything more
 * than a file and a line would start reproducing member-confidential text
 * one paraphrase at a time, which is exactly what this command must not do.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { SPEC_CITATIONS } from "../spec-index.generated.ts";
import { message, optionString } from "../discord.ts";

export const definition: CommandDefinition = {
	name: "spec",
	description: "Which docs/ files cite an Aliro 1.0 section. Never prints spec text",
	type: 1,
	options: [
		{
			name: "section",
			description: "e.g. 14, 11.3.1, or Table 8-3",
			type: 3,
			required: true,
		},
	],
};

/** `14`, `11.3.1`, `table 8-3`, `§8.3.3.5.1` all resolve to the stored form. */
export function normaliseSection(raw: string): string | null {
	const trimmed = raw.trim().replace(/^§\s*/, "");
	const table = /^table\s+(\d+-\d+)$/i.exec(trimmed);
	if (table) return `Table ${table[1]}`;
	if (/^\d+(\.\d+)*$/.test(trimmed)) return trimmed;
	return null;
}

/** Exact match, or a citation to a subsection of the queried section: asking
 *  for "11" should surface a citation to "11.7.3.4.1". Table numbers have no
 *  such hierarchy and only match exactly. */
function covers(query: string, cited: string): boolean {
	if (cited === query) return true;
	if (query.startsWith("Table ")) return false;
	return cited.startsWith(`${query}.`);
}

export function handler(c: CommandContext): Response {
	const raw = optionString(c.interaction, "section", 32);
	if (!raw) return message("Give me a section number, e.g. `14` or `11.3.1`.");

	const section = normaliseSection(raw);
	if (!section) {
		return message(
			"That is not a section number this command understands. Use a dotted number " +
				"like `11.3.1`, or `Table 8-3`.",
		);
	}

	const hits = SPEC_CITATIONS.filter((c) => covers(section, c.section));
	if (hits.length === 0) {
		return message(
			`No file in this repository cites Aliro 1.0 §${section}. That does not mean the ` +
				`spec is silent on it, only that nothing tracked here references it yet.`,
			{ ephemeral: false },
		);
	}

	const lines = hits.map((h) => `- \`${h.file}:${h.line}\` cites §${h.section}`);
	return message(
		`Aliro 1.0 §${section} is cited in ${hits.length} place${hits.length === 1 ? "" : "s"}:\n${lines.join("\n")}`,
		{ ephemeral: false },
	);
}
