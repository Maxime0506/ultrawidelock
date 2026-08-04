/**
 * @file `/test-request board: ios: what:` — maintainer-only.
 *
 * Looks owners up the same way `/who-has` does, partitions them into awake
 * and asleep from their stored UTC offset and awake window, posts a status
 * Container to the fixed test-queue channel (not wherever the command was
 * run — `TEST_QUEUE_CHANNEL_ID`), and pings only the awake half. Nobody's
 * identity appears in the persistent card itself, only aggregate counts,
 * matching this bot's "user IDs are not a browsable roster" posture even
 * though this command's whole job is finding and reaching specific people:
 * the card is what everyone in the channel sees, the ping is a disposable
 * message naming exactly the candidates being paged.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId, message, optionString } from "../discord.ts";
import { BOARDS, boardLabel, isKnownBoard, isValidIosVersion } from "../boards.ts";
import { RegistryUnavailable, whoHas } from "../rigs.ts";
import { isAwakeNow, nextWakeUnixMs } from "../awake.ts";
import { claim, createTestRequest, getRequest, RoutingUnavailable, setThread } from "../testRequests.ts";
import { buildAwakePing, buildTestRequestMessage, requestIdFromCustomId } from "../testRequestContainer.ts";
import { postMessage, startThreadFromMessage } from "../discordRest.ts";
import { defer, deferUpdate, type UpdateOutcome } from "../followup.ts";
import { isMaintainer } from "../maintainer.ts";

const MAX_WHAT = 200;

export const definition: CommandDefinition = {
	name: "test-request",
	description: "Maintainer only: route a hardware test to owners who are awake",
	type: 1,
	default_member_permissions: "0",
	options: [
		{
			name: "board",
			description: "Which board needs testing",
			type: 3,
			required: true,
			choices: BOARDS.map((b) => ({ name: b.name, value: b.value })),
		},
		{ name: "ios", description: "Filter by iOS version, e.g. 19.1", type: 3, required: false },
		{ name: "what", description: "One line: what needs testing", type: 3, required: true },
	],
};

export function handler(c: CommandContext): Response {
	const userId = invokerId(c.interaction);
	if (!userId || !isMaintainer(c.env, userId)) {
		return message("That command is maintainer only.");
	}

	const board = optionString(c.interaction, "board", 32);
	if (!board || !isKnownBoard(board)) {
		return message("That is not a board this registry knows about.");
	}
	const ios = optionString(c.interaction, "ios", 16);
	if (ios && !isValidIosVersion(ios)) {
		return message('iOS version has to look like "19.1" or "19".');
	}
	const what = optionString(c.interaction, "what", MAX_WHAT);
	if (!what) {
		return message("Say what needs testing in one line.");
	}

	const botToken = c.env.DISCORD_BOT_TOKEN;
	const queueChannelId = c.env.TEST_QUEUE_CHANNEL_ID;
	if (!botToken || !queueChannelId) {
		return message("Test-request routing is not configured (missing bot token or queue channel). Nothing was posted.");
	}

	return defer(c, async (ctx) => {
		let owners;
		try {
			owners = await whoHas(ctx.env.DB, { board, iosVersion: ios ?? undefined });
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${ctx.correlationId}] test-request registry lookup failed:`, err.cause ?? err);
				return `The registry is not reachable right now. Quote \`${ctx.correlationId}\` if it keeps failing.`;
			}
			throw err;
		}

		if (owners.length === 0) {
			return "Nobody in the registry matches that. Nothing was posted.";
		}

		const now = Date.now();
		const awake = owners.filter((o) => isAwakeNow(o.utc_offset, o.awake_start, o.awake_end, now));
		const asleep = owners.filter((o) => !isAwakeNow(o.utc_offset, o.awake_start, o.awake_end, now));

		const nextWake =
			asleep.length > 0
				? Math.min(...asleep.map((o) => nextWakeUnixMs(o.utc_offset, o.awake_start, now)))
				: null;

		const requestId = crypto.randomUUID();
		const containerBody = buildTestRequestMessage({
			requestId,
			board,
			iosVersion: ios,
			what,
			status: "pending",
			awakeCount: awake.length,
			asleepCount: asleep.length,
			nextWakeUnixMs: nextWake,
		});

		const posted = await postMessage(botToken, ctx.correlationId, queueChannelId, containerBody);
		if (!posted) {
			return `Could not post to the test queue channel. Nothing was recorded. Quote \`${ctx.correlationId}\` if it keeps failing.`;
		}

		if (awake.length > 0) {
			const ping = buildAwakePing(awake.map((o) => o.discord_user_id));
			await postMessage(botToken, ctx.correlationId, queueChannelId, ping);
			// A failed ping is not fatal: the card is posted and visible either
			// way, and it is logged inside postMessage already.
		}

		try {
			await createTestRequest(ctx.env.DB, {
				id: requestId,
				requesterId: userId,
				board,
				iosVersion: ios,
				what,
				channelId: posted.channel_id,
				messageId: posted.id,
				createdAt: now,
				candidates: [
					...awake.map((o) => ({ discordUserId: o.discord_user_id, awake: true })),
					...asleep.map((o) => ({ discordUserId: o.discord_user_id, awake: false })),
				],
			});
		} catch (err) {
			if (err instanceof RoutingUnavailable) {
				console.error(`[${ctx.correlationId}] test-request D1 write failed after posting:`, err.cause ?? err);
				return (
					`Posted to <#${queueChannelId}>, but the routing state could not be saved, so Accept won't work on it. ` +
					`Quote \`${ctx.correlationId}\` if it keeps failing.`
				);
			}
			throw err;
		}

		return `Posted to <#${queueChannelId}>: ${awake.length} awake now, ${asleep.length} asleep.`;
	});
}

/** The Accept button's own handler: first click wins (claim() is an atomic
 *  UPDATE...WHERE guard), starts a thread, and edits the card in place to
 *  CLAIMED. A losing click never touches the card — it would clobber
 *  whichever edit the winner's click already wrote — and gets a private
 *  "someone already accepted this" instead. */
export function componentHandler(c: CommandContext): Response {
	const requestId = requestIdFromCustomId(c.interaction.data?.custom_id ?? "");
	const userId = invokerId(c.interaction);
	if (!requestId || !userId) {
		return message("That button has expired or was tampered with.", { ephemeral: true });
	}

	return deferUpdate(c, async (ctx): Promise<UpdateOutcome> => {
		let claimed: boolean;
		try {
			claimed = await claim(ctx.env.DB, requestId, userId);
		} catch (err) {
			if (err instanceof RoutingUnavailable) {
				console.error(`[${ctx.correlationId}] accept claim failed:`, err.cause ?? err);
				return { ephemeralNote: `The registry is not reachable right now, so nothing was claimed. Quote \`${ctx.correlationId}\` if it keeps failing.` };
			}
			throw err;
		}
		if (!claimed) {
			return { ephemeralNote: "Someone already accepted this one." };
		}

		const req = await getRequest(ctx.env.DB, requestId);
		if (!req) {
			// claim() just succeeded against this id, so this should not
			// happen; guard anyway rather than dereferencing null below.
			console.error(`[${ctx.correlationId}] claimed ${requestId} but it does not exist`);
			return { ephemeralNote: `Claimed, but the request record could not be read back. Quote \`${ctx.correlationId}\`.` };
		}

		if (ctx.env.DISCORD_BOT_TOKEN) {
			const threadName = `test: ${boardLabel(req.board)}${req.ios_version ? ` iOS ${req.ios_version}` : ""}`;
			const thread = await startThreadFromMessage(
				ctx.env.DISCORD_BOT_TOKEN,
				ctx.correlationId,
				req.channel_id,
				req.message_id,
				threadName,
			);
			if (thread) {
				await setThread(ctx.env.DB, requestId, thread.id);
			}
			// A failed thread creation does not block the claim itself — the
			// card still needs to show CLAIMED either way, and the failure is
			// already logged inside startThreadFromMessage.
		}

		return {
			body: buildTestRequestMessage({
				requestId,
				board: req.board,
				iosVersion: req.ios_version,
				what: req.what,
				status: "claimed",
				awakeCount: 0,
				asleepCount: 0,
				nextWakeUnixMs: null,
				claimedBy: userId,
			}),
		};
	});
}

// Declared here rather than in a table in commands/index.ts, so that a button
// handler cannot exist with nothing routing to it. See command.ts.
export const componentPrefix = "test-accept";
export const onComponent = componentHandler;
