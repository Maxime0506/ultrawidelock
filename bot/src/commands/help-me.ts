/**
 * @file `/help-me` — collect the context once, match it, escalate honestly.
 *
 * The whole point of this bot. Support here costs three or four round trips
 * before anyone knows which board, which image, and what the console said, so
 * this asks for all of it in one form and posts a thread that already has the
 * answer or already says there isn't one.
 *
 * Two interactions, not one. Discord will not let a command defer and then
 * open a modal, so board and image are command options (enumerated, validated
 * by Discord) and the free text is collected by the modal that the command
 * returns. The selections ride through on the modal's custom_id.
 *
 * The matcher never speculates. No match is reported as no match and pings the
 * maintainer, because "I don't recognise this" is a useful thing to tell
 * somebody and a plausible guess is not.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { BOARDS, boardLabel, isKnownBoard } from "../boards.ts";
import { IMAGES, imageLabel, isKnownImage } from "../images.ts";
import { createForumThread, forumChannelFor, postToThread } from "../api.ts";
import { claimInteraction, RegistryUnavailable } from "../db.ts";
import { entriesForUser } from "../rigs.ts";
import { formatCitations } from "../citations.ts";
import { matchSignatures } from "../signatures.ts";
import {
	TextInputStyle,
	invokerId,
	message,
	modal,
	modalValue,
	NO_MENTIONS,
	onlyUsers,
	optionString,
} from "../discord.ts";
import { defer } from "../followup.ts";

/** Discord's own ceiling for a text input is 4000. */
const MAX_CONSOLE = 4000;
const MAX_EXPECTED = 300;
const MAX_ACTUAL = 700;
/** Discord's ceiling for one message. */
const MAX_MESSAGE = 2000;
const MAX_THREAD_NAME = 100;
/** How many matches to print. Beyond this the paste matched so much that the
 *  ranking is the useful output, not the list. */
const MAX_SHOWN = 4;

export const MODAL_PREFIX = "help-me";
export const modalPrefix = MODAL_PREFIX;

export const definition: CommandDefinition = {
	name: "help-me",
	description: "Report a problem: collects the context once and opens a thread",
	type: 1,
	options: [
		{
			name: "board",
			description: "Which board",
			type: 3,
			required: true,
			choices: BOARDS.map((b) => ({ name: b.name, value: b.value })),
		},
		{
			name: "image",
			description: "Which image you are running",
			type: 3,
			required: true,
			choices: IMAGES.map((i) => ({ name: i.name, value: i.value })),
		},
		{
			name: "ping_maintainer",
			description: "Ping the maintainer even if a known signature matches",
			type: 5,
			required: false,
		},
	],
};

function optionBoolean(c: CommandContext, name: string): boolean {
	return c.interaction.data?.options?.find((o) => o.name === name)?.value === true;
}

export function handler(c: CommandContext): Response {
	const board = optionString(c.interaction, "board", 64);
	const image = optionString(c.interaction, "image", 64);

	if (!board || !isKnownBoard(board) || !image || !isKnownImage(image)) {
		return message("That is not a board and image this bot knows about.");
	}

	const ping = optionBoolean(c, "ping_maintainer") ? "1" : "0";

	return modal(`${MODAL_PREFIX}|${board}|${image}|${ping}`, "Report a problem", [
		{
			customId: "expected",
			label: "What did you expect to happen?",
			style: TextInputStyle.Short,
			required: true,
			maxLength: MAX_EXPECTED,
			placeholder: "The phone unlocks the door on approach",
		},
		{
			customId: "actual",
			label: "What happened instead?",
			style: TextInputStyle.Paragraph,
			required: true,
			maxLength: MAX_ACTUAL,
			placeholder: "Tap works, approach never ranges",
		},
		{
			customId: "console",
			label: "Console output",
			style: TextInputStyle.Paragraph,
			required: false,
			maxLength: MAX_CONSOLE,
			placeholder: "Paste from make monitor. Longer than 4000 characters gets truncated.",
		},
	]);
}

interface Selections {
	board: string;
	image: string;
	ping: boolean;
}

/** Parse what the modal carried through, rejecting anything not on the lists. */
export function parseModalId(customId: string): Selections | null {
	const [prefix, board, image, ping] = customId.split("|");
	if (prefix !== MODAL_PREFIX || !board || !image) return null;
	if (!isKnownBoard(board) || !isKnownImage(image)) return null;
	return { board, image, ping: ping === "1" };
}

export function onModalSubmit(c: CommandContext): Response {
	const selections = parseModalId(c.interaction.data?.custom_id ?? "");
	if (!selections) {
		return message("That form did not carry a board and image I recognise. Nothing was posted.");
	}

	const userId = invokerId(c.interaction);
	const expected = modalValue(c.interaction, "expected", MAX_EXPECTED) ?? "(not given)";
	const actual = modalValue(c.interaction, "actual", MAX_ACTUAL) ?? "(not given)";
	const consoleRaw = modalValue(c.interaction, "console", MAX_CONSOLE);

	return defer(c, async () => {
		// Discord retries a delivery it did not hear back about, and a retry must
		// not open a second thread. Losing the claim to a dead D1 is survivable;
		// losing the report is not, so an unreachable registry proceeds.
		try {
			const mine = await claimInteraction(c.env.DB, c.interaction.id);
			if (!mine) {
				return "That report was already posted. This was a duplicate delivery from Discord, not a second submission.";
			}
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] dedupe unavailable, proceeding:`, err.cause ?? err);
			} else {
				throw err;
			}
		}

		const matches = matchSignatures(`${actual}\n${consoleRaw ?? ""}`);
		const maintainer = (c.env.MAINTAINER_IDS ?? "").split(/[\s,]+/).filter(Boolean)[0];
		const shouldPing = matches.length === 0 || selections.ping;

		// The registry lookup is the only part allowed to go missing. Everything
		// below still happens when D1 is down.
		let hardware: string | null = null;
		let registryLost = false;
		if (userId) {
			try {
				// rigs.ts has no single-board lookup: `rigs` is keyed on
				// (user, board) so a contributor legitimately has several rows,
				// and every other caller wants all of them. Filtering one out
				// here is cheaper than a second query path nothing else uses.
				const mine = (await entriesForUser(c.env.DB, userId)).find(
					(r) => r.board === selections.board,
				);
				hardware =
					[mine?.phone_model, mine?.ios_version ? `iOS ${mine.ios_version}` : null]
						.filter(Boolean)
						.join(", ") || null;
			} catch {
				registryLost = true;
			}
		}

		const block = contextBlock({
			selections,
			userId,
			expected,
			actual,
			matches,
			hardware,
			registryLost,
			consoleLength: consoleRaw?.length ?? 0,
			maintainer: shouldPing ? maintainer : undefined,
		});

		const channelId = forumChannelFor(c.env, selections.board);
		if (!channelId) {
			console.error(`[${c.correlationId}] no forum channel for ${selections.board}`);
			return (
				`No forum channel is configured for ${boardLabel(selections.board)}, so I could not ` +
				`open a thread. Here is the context block to paste yourself:\n\n${block}`
			).slice(0, MAX_MESSAGE);
		}

		const threadId = await createForumThread(
			c.env,
			channelId,
			threadName(selections.board, expected, actual),
			block,
			shouldPing && maintainer ? onlyUsers([maintainer]) : NO_MENTIONS,
			c.correlationId,
		);

		if (!threadId) {
			return (
				`I could not open the thread. Nothing was lost: here is the context block ` +
				`to paste yourself. Quote \`${c.correlationId}\` when reporting this.\n\n${block}`
			).slice(0, MAX_MESSAGE);
		}

		// Best effort, and deliberately second: the thread already carries the
		// context block, so a failure here costs the paste and not the report.
		if (consoleRaw) {
			await postToThread(c.env, threadId, consoleBlock(consoleRaw), c.correlationId);
		}

		return `Opened <#${threadId}>. Everything you typed is in it, along with ${
			matches.length === 0 ? "a note that nothing matched" : `${matches.length} matched signature(s)`
		}.`;
	});
}

/** Discord caps a thread name at 100 characters and newlines read badly in a
 *  channel list. */
export function threadName(board: string, expected: string, actual: string): string {
	const summary = (actual || expected).replace(/\s+/g, " ").trim();
	return `${boardLabel(board)}: ${summary}`.slice(0, MAX_THREAD_NAME);
}

/** The console paste, fenced, with the truncation stated rather than implied. */
export function consoleBlock(text: string): string {
	// Fence, language hint, newlines and the note all come out of the budget.
	const budget = MAX_MESSAGE - 120;
	const clipped = text.length > budget;
	const body = clipped ? text.slice(0, budget) : text;
	// A fence inside the paste would end the block early.
	const safe = body.replace(/```/g, "``​`");
	return (
		"```\n" +
		safe +
		"\n```" +
		(clipped ? `\n-# truncated to ${budget} of ${text.length} characters` : "")
	);
}

interface BlockInput {
	selections: Selections;
	userId: string | null;
	expected: string;
	actual: string;
	matches: ReturnType<typeof matchSignatures>;
	hardware: string | null;
	registryLost: boolean;
	consoleLength: number;
	maintainer: string | undefined;
}

/**
 * The context block.
 *
 * Says what is known, what matched and why, and what is still unknown. The
 * last part matters: a report that looks complete but silently omits the NCS
 * pin costs the round trip this bot exists to remove.
 */
export function contextBlock(input: BlockInput): string {
	const { selections, userId, expected, actual, matches, maintainer } = input;

	// Three parts with different rights to the 2000 characters. The head and
	// the tail are guaranteed; the match list gets what is left and is trimmed
	// with a note saying how many it dropped.
	//
	// The order matters. A trailing `.slice(2000)` over the whole thing looks
	// equivalent and is not: it silently ate the "Still unknown" line and the
	// maintainer ping on a long paste, which are the two parts a reader most
	// needs and the two least likely to be missed.
	const head: string[] = [
		`**${boardLabel(selections.board)}** · ${imageLabel(selections.image)}`,
		userId ? `Reported by <@${userId}>` : "Reporter unknown",
		"",
		`**Expected:** ${expected}`,
		`**Actual:** ${actual}`,
	];
	if (input.hardware) head.push(`**Registered hardware:** ${input.hardware}`);
	head.push("");

	const unknown = ["host OS", "NCS version", "firmware commit"];
	if (input.consoleLength === 0) unknown.push("console output (none pasted)");
	if (input.registryLost) unknown.push("registered hardware (the registry was unreachable)");
	else if (!input.hardware) unknown.push("phone and iOS version (nothing registered)");

	const tail: string[] = ["", `**Still unknown:** ${unknown.join(", ")}. Ask before assuming.`];
	if (maintainer) tail.push("", `<@${maintainer}>`);

	const headText = head.join("\n");
	const tailText = tail.join("\n");
	const budget = MAX_MESSAGE - headText.length - tailText.length - 1;

	return [headText, matchesSection(matches, budget), tailText].join("\n");
}

/** The match list, trimmed to `budget` characters, saying what it dropped. */
function matchesSection(matches: BlockInput["matches"], budget: number): string {
	if (matches.length === 0) {
		return (
			"**No known signature.** Nothing here matches anything documented in this " +
			"repository, so this is escalating rather than guessing."
		).slice(0, Math.max(0, budget));
	}

	const header = `**Matched ${matches.length} known signature${matches.length === 1 ? "" : "s"}**, most specific first:`;
	const entries = matches.slice(0, MAX_SHOWN).map((m) =>
		[
			`**${m.signature.id}** — matched \`${m.matched.replace(/`/g, "'").slice(0, 60)}\``,
			m.signature.reading,
			`Next: ${m.signature.next}`,
			`-# ${formatCitations(m.signature.citations)}`,
		].join("\n"),
	);

	const lines = [header];
	let used = header.length;
	let shown = 0;

	for (const entry of entries) {
		// Reserve room for the "not shown" note, which has to survive even when
		// it is the reason the reader knows something is missing.
		const note = `\n-# ${matches.length - shown} further match(es) not shown.`;
		if (used + entry.length + 2 + note.length > budget) break;
		lines.push("", entry);
		used += entry.length + 2;
		shown++;
	}

	const hidden = matches.length - shown;
	if (hidden > 0) lines.push("", `-# ${hidden} further match(es) not shown.`);

	return lines.join("\n");
}
