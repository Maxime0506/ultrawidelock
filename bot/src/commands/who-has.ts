/**
 * @file `/who-has` — maintainer-only lookup.
 *
 * Returns user IDs to ping, not a browsable roster: `default_member_permissions:
 * "0"` hides it from everyone without guild administrator rights, and the
 * handler checks the invoker against MAINTAINER_IDS regardless, because the
 * first is a server setting somebody can change and the second is not.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId, message, optionString } from "../discord.ts";
import { BOARDS, boardLabel, isKnownBoard, isValidIosVersion, nfcLabel, radioLabel } from "../boards.ts";
import { RegistryUnavailable, WHO_HAS_LIMIT, whoHas } from "../rigs.ts";
import { defer } from "../followup.ts";
import { isMaintainer } from "../maintainer.ts";

export const definition: CommandDefinition = {
	name: "who-has",
	description: "Maintainer only: find contributors by board and/or iOS version",
	type: 1,
	default_member_permissions: "0",
	options: [
		{
			name: "board",
			description: "Filter by board",
			type: 3,
			required: false,
			choices: BOARDS.map((b) => ({ name: b.name, value: b.value })),
		},
		{ name: "ios", description: "Filter by iOS version, e.g. 19.1", type: 3, required: false },
	],
};

export function handler(c: CommandContext): Response {
	const userId = invokerId(c.interaction);
	if (!userId || !isMaintainer(c.env, userId)) {
		// Same answer either way: whether the command exists is not a secret,
		// but who is on the maintainer list does not need confirming here.
		return message("That command is maintainer only.");
	}

	const board = optionString(c.interaction, "board", 32);
	if (board && !isKnownBoard(board)) {
		return message("That is not a board this registry knows about.");
	}
	const ios = optionString(c.interaction, "ios", 16);
	if (ios && !isValidIosVersion(ios)) {
		return message('iOS version has to look like "19.1" or "19".');
	}
	if (!board && !ios) {
		return message("Give me a board, an iOS version, or both.");
	}

	return defer(c, async () => {
		let rows;
		try {
			rows = await whoHas(c.env.DB, { board: board ?? undefined, iosVersion: ios ?? undefined });
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] registry search failed:`, err.cause ?? err);
				return (
					`The registry is not reachable right now, so this returned nothing rather ` +
					`than nobody. Quote \`${c.correlationId}\` if it keeps failing.`
				);
			}
			throw err;
		}

		if (rows.length === 0) {
			return "Nobody in the registry matches that.";
		}

		const lines = rows.map((r) => {
			const bits = [
				boardLabel(r.board),
				radioLabel(r.radio),
				`NFC ${nfcLabel(r.nfc)}`,
				r.phone_model,
				r.ios_version ? `iOS ${r.ios_version}` : null,
				`awake ${r.awake_start}-${r.awake_end} local (UTC${r.utc_offset >= 0 ? "+" : ""}${r.utc_offset / 60})`,
			].filter(Boolean);
			// Mentions are suppressed on every message this bot sends, so this
			// renders as a name without pinging anyone. Copy the ID to ping.
			return `- <@${r.discord_user_id}> ${bits.join(" · ")}`;
		});

		const capped = rows.length === WHO_HAS_LIMIT;
		const total = capped ? `at least ${rows.length}` : `${rows.length}`;
		const more = capped ? `\n-# capped at ${WHO_HAS_LIMIT}, there may be more` : "";
		return `${total} match${rows.length === 1 ? "" : "es"}:\n${lines.join("\n")}${more}`;
	});
}
