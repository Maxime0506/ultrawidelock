/**
 * @file The `/test-request` status card, and the one-time ping that goes
 * with it — kept as two separate messages rather than one, so the ping
 * (disposable, mentions-bearing, ordinary `content`) never has to be
 * reconstructed when the card (persistent, Components V2, no `content`
 * allowed at all per the platform table this bot's spec cites) is edited in
 * place later.
 *
 * Component shapes below are per docs.discord.com/developers/components
 * (checked 2026-08-04): a Section (type 9)'s `accessory` is the documented
 * way a Components V2 message carries a Button next to text, not an Action
 * Row inside a Container — the docs show no Container -> Action Row nesting
 * at all, so that path was not used here. This has not been proven against
 * a live Discord render, the same caveat as this bot's modal wire format.
 */
import { boardLabel } from "./boards.ts";
import { MessageFlags } from "./discord.ts";

export const ACCENT_COLOR = {
	pending: 0xf1c40f,
	claimed: 0x5865f2,
	pass: 0x2ecc71,
	fail: 0xe74c3c,
} as const;

const ACCEPT_PREFIX = "test-accept";

export function acceptCustomId(requestId: string): string {
	return `${ACCEPT_PREFIX}:${requestId}`;
}

/** `test-accept:<uuid>` -> the request id, or null if this is not one of
 *  this bot's Accept buttons. */
export function requestIdFromCustomId(customId: string): string | null {
	const parts = customId.split(":");
	if (parts.length !== 2 || parts[0] !== ACCEPT_PREFIX) return null;
	return parts[1] || null;
}

export interface ContainerInput {
	requestId: string;
	board: string;
	iosVersion: string | null;
	what: string;
	status: "pending" | "claimed" | "done";
	awakeCount: number;
	asleepCount: number;
	/** Null when there is nobody left asleep to wait on. */
	nextWakeUnixMs: number | null;
	claimedBy?: string | null;
	/** Only meaningful once status is "done". */
	passed?: boolean;
}

function statusLine(input: ContainerInput): string {
	if (input.status === "done") {
		return input.passed ? `✅ **PASSED** · tested by <@${input.claimedBy}>` : `❌ **FAILED** · tested by <@${input.claimedBy}>`;
	}
	if (input.status === "claimed") {
		return `🔵 **CLAIMED** by <@${input.claimedBy}>`;
	}
	const asleepNote =
		input.asleepCount > 0 && input.nextWakeUnixMs !== null
			? `, ${input.asleepCount} asleep · next wakes <t:${Math.floor(input.nextWakeUnixMs / 1000)}:R>`
			: "";
	return `🟡 **PENDING** · ${input.awakeCount} awake now${asleepNote}`;
}

function accentColor(input: ContainerInput): number {
	if (input.status === "done") return input.passed ? ACCENT_COLOR.pass : ACCENT_COLOR.fail;
	return ACCENT_COLOR[input.status];
}

function accessoryButton(input: ContainerInput): Record<string, unknown> {
	if (input.status === "done") {
		return {
			type: 2,
			style: input.passed ? 3 : 4, // Success / Danger
			label: input.passed ? "Passed" : "Failed",
			custom_id: acceptCustomId(input.requestId),
			disabled: true,
		};
	}
	const claimed = input.status === "claimed";
	return {
		type: 2,
		style: claimed ? 2 : 3, // Secondary once claimed, Success while open
		label: claimed ? "Claimed" : "Accept",
		custom_id: acceptCustomId(input.requestId),
		disabled: claimed,
	};
}

/** The Components V2 message body for the status card: a Container whose
 *  accent color and Accept button state track `status`. Same shape whether
 *  this is the first post or a later in-place edit. */
export function buildTestRequestMessage(input: ContainerInput): { flags: number; components: unknown[] } {
	const title = `**Test request** · ${boardLabel(input.board)}${input.iosVersion ? ` · iOS ${input.iosVersion}` : ""}`;

	return {
		flags: MessageFlags.IsComponentsV2,
		components: [
			{
				type: 17, // Container
				accent_color: accentColor(input),
				components: [
					{ type: 10, content: title }, // Text Display
					{ type: 10, content: input.what }, // Text Display
					{ type: 14 }, // Separator
					{
						type: 9, // Section
						components: [{ type: 10, content: statusLine(input) }],
						accessory: accessoryButton(input),
					},
				],
			},
		],
	};
}

/** The disposable ping that accompanies the first post: ordinary `content`
 *  (not Components V2, so it can carry mentions at all), with an explicit
 *  `allowed_mentions.users` allow-list rather than relying on the default
 *  parse rules — the same "never let user text create a ping" posture as
 *  every other message this bot sends, just inverted to explicitly *permit*
 *  exactly the candidate IDs this Worker itself looked up, not whatever a
 *  free-text field happened to contain. */
export function buildAwakePing(candidateIds: readonly string[]): { content: string; allowed_mentions: unknown } {
	const mentions = candidateIds.map((id) => `<@${id}>`).join(" ");
	return {
		content: `🔔 ${mentions} — new test request above, first to Accept gets it.`,
		allowed_mentions: { parse: [], users: candidateIds },
	};
}

export function buildEscalationPing(candidateIds: readonly string[]): { content: string; allowed_mentions: unknown } {
	const mentions = candidateIds.map((id) => `<@${id}>`).join(" ");
	return {
		content: `🔔 ${mentions} — still open, nobody has accepted yet.`,
		allowed_mentions: { parse: [], users: candidateIds },
	};
}
