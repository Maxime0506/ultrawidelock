/**
 * @file `/test-result pass|fail` — run inside the claim thread, by whoever
 * claimed it. Closes the job (`claimed -> done`, atomically, so a retried
 * or duplicate delivery cannot record two validations for one claim),
 * writes a validation row, and edits the original queue Container to a
 * pass/fail accent — the one edit in this bot that a component's own
 * interaction token cannot make, since this command runs in the thread, a
 * different channel from the card it needs to update, so it goes through
 * the bot token like the escalation sweep does.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId, message, optionString } from "../discord.ts";
import { boardLabel } from "../boards.ts";
import { getRequestByThreadId, markDone, RoutingUnavailable } from "../testRequests.ts";
import { recordValidation, ValidationsUnavailable } from "../validations.ts";
import { buildTestRequestMessage } from "../testRequestContainer.ts";
import { editMessage } from "../discordRest.ts";
import { defer } from "../followup.ts";

export const definition: CommandDefinition = {
	name: "test-result",
	description: "Record pass/fail for the test-request thread you're in",
	type: 1,
	options: [
		{
			name: "result",
			description: "Did it pass?",
			type: 3,
			required: true,
			choices: [
				{ name: "pass", value: "pass" },
				{ name: "fail", value: "fail" },
			],
		},
	],
};

export function handler(c: CommandContext): Response {
	const threadId = c.interaction.channel_id;
	const userId = invokerId(c.interaction);
	const resultRaw = optionString(c.interaction, "result", 8);

	if (!threadId || !userId) {
		return message("Could not tell which channel or user this is. Nothing was recorded.");
	}
	if (resultRaw !== "pass" && resultRaw !== "fail") {
		return message('Result has to be "pass" or "fail".');
	}
	const passed = resultRaw === "pass";

	return defer(c, async (ctx) => {
		let req;
		try {
			req = await getRequestByThreadId(ctx.env.DB, threadId);
		} catch (err) {
			if (err instanceof RoutingUnavailable) {
				console.error(`[${ctx.correlationId}] test-result lookup failed:`, err.cause ?? err);
				return `The registry is not reachable right now. Quote \`${ctx.correlationId}\` if it keeps failing.`;
			}
			throw err;
		}

		if (!req) {
			return "This only works inside a test-request thread — run it in the thread the Accept button opened.";
		}
		if (req.status === "done") {
			return "This request already has a result recorded.";
		}
		if (req.status !== "claimed") {
			return "This request has not been claimed yet, so there is nothing to close out.";
		}
		if (req.claimed_by !== userId) {
			// Mentions are suppressed on every message this bot sends, so this
			// renders as a name without pinging anyone.
			return `Only <@${req.claimed_by}> (whoever accepted this) can record the result.`;
		}

		let closed: boolean;
		try {
			closed = await markDone(ctx.env.DB, req.id);
		} catch (err) {
			if (err instanceof RoutingUnavailable) {
				console.error(`[${ctx.correlationId}] test-result markDone failed:`, err.cause ?? err);
				return `The registry is not reachable right now, so nothing was recorded. Quote \`${ctx.correlationId}\` if it keeps failing.`;
			}
			throw err;
		}
		if (!closed) {
			return "This request already has a result recorded.";
		}

		const now = Date.now();
		let matrixNote = "";
		if (req.ios_version) {
			try {
				await recordValidation(ctx.env.DB, {
					id: crypto.randomUUID(),
					board: req.board,
					iosVersion: req.ios_version,
					passed,
					testedBy: userId,
					requestId: req.id,
					testedAt: now,
				});
			} catch (err) {
				if (err instanceof ValidationsUnavailable) {
					console.error(`[${ctx.correlationId}] test-result validation write failed:`, err.cause ?? err);
					matrixNote = " (the result could not be saved for the matrix — closed anyway; nothing was lost about the claim itself)";
				} else {
					throw err;
				}
			}
		} else {
			matrixNote = " (the original request did not specify an iOS version, so this will not appear on `/matrix`)";
		}

		if (ctx.env.DISCORD_BOT_TOKEN) {
			const cardBody = buildTestRequestMessage({
				requestId: req.id,
				board: req.board,
				iosVersion: req.ios_version,
				what: req.what,
				status: "done",
				awakeCount: 0,
				asleepCount: 0,
				nextWakeUnixMs: null,
				claimedBy: userId,
				passed,
			});
			await editMessage(ctx.env.DISCORD_BOT_TOKEN, ctx.correlationId, req.channel_id, req.message_id, cardBody);
			// A failed edit is logged inside editMessage; the D1 state is
			// already correctly closed out either way.
		}

		return `Recorded **${passed ? "PASS" : "FAIL"}** for ${boardLabel(req.board)}${req.ios_version ? ` iOS ${req.ios_version}` : ""}.${matrixNote}`;
	});
}
