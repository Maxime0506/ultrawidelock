/**
 * @file `/decode-devid` — the most common bring-up failure.
 *
 * `make selftest` logs the raw DEV_ID read from the DW3110 over SPI. The tree
 * documents exactly two outcomes for that value, and this command encodes those
 * two. Anything else is reported as unrecognised: a DEV_ID that is neither the
 * healthy prefix nor one of the two dead reads is a fact nobody here has
 * written down, and inventing a reading for it is how a triage bot starts
 * costing people evenings.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { DEVID, formatCitations } from "../citations.ts";
import { message, optionString } from "../discord.ts";

export const definition: CommandDefinition = {
	name: "decode-devid",
	description: "Read a raw DEV_ID from make selftest and say what it means",
	type: 1,
	options: [
		{
			name: "value",
			description: "The raw DEV_ID, e.g. 0xDECA0302",
			type: 3,
			required: true,
		},
	],
};

const CITED = formatCitations(DEVID.citations);

/**
 * Accept `0xDECA0302`, `DECA0302`, `deca0302`, and short forms, but nothing
 * that is not hex. Returns 8 lowercase hex digits, or null.
 *
 * Only the ends are trimmed. Internal whitespace is a rejection rather than
 * something to strip: a paste of two values would otherwise be spliced into a
 * third that nobody's board ever reported, and answered confidently.
 */
export function normalise(raw: string): string | null {
	const stripped = raw.trim().replace(/^0[xX]/, "");
	if (!/^[0-9a-fA-F]{1,8}$/.test(stripped)) return null;
	return stripped.toLowerCase().padStart(8, "0");
}

export function handler(c: CommandContext): Response {
	const raw = optionString(c.interaction, "value", 32);
	if (!raw) {
		return message("Give me the DEV_ID that `make selftest` printed.");
	}

	const value = normalise(raw);
	if (!value) {
		// The input is not echoed: it failed a hex test, so whatever it is, it
		// is not a DEV_ID.
		return message(
			"That is not a hex value. Paste the raw DEV_ID exactly as the log printed it, " +
				"for example `0xDECA0302`.",
		);
	}

	const pretty = `0x${value.toUpperCase()}`;

	if (value.startsWith(DEVID.healthyPrefix)) {
		return message(
			`**${pretty} — the DW3110 is answering.**\n` +
				`That is the expected read, so SPI, the pin map and power to the part are all fine. ` +
				`If ranging still fails, the problem is above the driver.\n` +
				`-# ${CITED}`,
			{ ephemeral: false },
		);
	}

	if (DEVID.deadValues.includes(value)) {
		return message(
			`**${pretty} — the DW3110 is not answering.**\n` +
				`That is one of the two documented dead reads. It means a wrong pin, a wrong ` +
				`SPI mode, or an unpowered DW3110. It does not narrow further than those three.\n` +
				`**Next:** rebuild the self-test and watch it directly with ` +
				`\`make selftest\` then \`make monitor CDK_RTT_BUILD=build/cdk-selftest\`.\n` +
				`-# ${CITED}`,
			{ ephemeral: false },
		);
	}

	return message(
		`**${pretty} — no known signature.**\n` +
			`This repository documents a healthy read of \`0xDECA03xx\` and two dead reads, ` +
			`\`0x00000000\` and \`0xFFFFFFFF\`. Yours is none of those, so I have nothing ` +
			`traceable to say about it and will not guess.\n` +
			`**Next:** open a thread with \`/help-me\` so a human sees it.\n` +
			`-# ${CITED}`,
		{ ephemeral: false },
	);
}
