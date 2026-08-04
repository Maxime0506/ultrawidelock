/**
 * @file `/forget` — hard delete.
 *
 * Not a flag, not a soft delete, not a tombstone. The row goes. With no
 * `board` argument, every row for the invoker goes.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId, message, optionString } from "../discord.ts";
import { BOARDS, boardLabel, isKnownBoard } from "../boards.ts";
import { RegistryUnavailable, forgetRig } from "../rigs.ts";
import { defer } from "../followup.ts";

export const definition: CommandDefinition = {
	name: "forget",
	description: "Delete a board you registered, or everything, immediately and permanently",
	type: 1,
	options: [
		{
			name: "board",
			description: "Leave empty to delete every board you have registered",
			type: 3,
			required: false,
			choices: BOARDS.map((b) => ({ name: b.name, value: b.value })),
		},
	],
};

export function handler(c: CommandContext): Response {
	const userId = invokerId(c.interaction);
	if (!userId) {
		return message("Could not tell who invoked that, so nothing was deleted.");
	}

	const board = optionString(c.interaction, "board", 32);
	if (board && !isKnownBoard(board)) {
		return message("That is not a board this registry knows about. Nothing was deleted.");
	}

	return defer(c, async () => {
		let removed: number;
		try {
			removed = await forgetRig(c.env.DB, userId, board ?? undefined);
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] registry delete failed:`, err.cause ?? err);
				return (
					`The registry is not reachable right now, so **nothing was deleted**. ` +
					`Run \`/forget\` again in a few minutes. Quote \`${c.correlationId}\` if it keeps failing.`
				);
			}
			throw err;
		}

		if (removed === 0) {
			return board
				? `You had no **${boardLabel(board)}** entry to delete.`
				: "You had nothing registered, so there was nothing to delete.";
		}
		if (board) {
			return `Deleted your **${boardLabel(board)}** entry.`;
		}
		return `Deleted ${removed} ${removed === 1 ? "entry" : "entries"}. Nothing about you is left in the registry.`;
	});
}
