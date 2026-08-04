/**
 * @file `/why` — the recurring "is this a bug?" answers.
 *
 * Every one of these is a thing the tree already says, that a contributor has
 * no reason to have read. Answers publicly rather than ephemerally: the point
 * is that the next person sees it too.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { TOPICS, formatCitations } from "../citations.ts";
import { message, optionString } from "../discord.ts";

export const definition: CommandDefinition = {
	name: "why",
	description: "Answer a recurring question about expected behaviour, with a citation",
	type: 1,
	options: [
		{
			name: "topic",
			description: "Which behaviour",
			type: 3,
			required: true,
			choices: TOPICS.map((t) => ({ name: t.label, value: t.id })),
		},
	],
};

export function handler(c: CommandContext): Response {
	const id = optionString(c.interaction, "topic", 64);
	const topic = TOPICS.find((t) => t.id === id);

	if (!topic) {
		// Discord validates choices, so this is a stale client or a crafted
		// payload. Listing what does exist is more useful than echoing what did
		// not, and echoes nothing the caller typed.
		return message(
			`I do not have an entry for that. What I do have: ` +
				TOPICS.map((t) => `\`${t.id}\``).join(", ") +
				`.`,
		);
	}

	return message(
		`**${topic.label}**\n` +
			`${topic.reading}\n\n` +
			`**Next:** ${topic.next}\n` +
			`-# ${formatCitations(topic.citations)}`,
		{ ephemeral: false },
	);
}
