/**
 * @file Wire constants and response builders for the Discord interactions
 * protocol. Kept dependency-free so `test/` can exercise it without a
 * Worker runtime.
 */

export const InteractionType = {
	Ping: 1,
	ApplicationCommand: 2,
	MessageComponent: 3,
	ApplicationCommandAutocomplete: 4,
	ModalSubmit: 5,
} as const;

export const InteractionResponseType = {
	Pong: 1,
	ChannelMessageWithSource: 4,
	DeferredChannelMessageWithSource: 5,
	DeferredUpdateMessage: 6,
	UpdateMessage: 7,
	ApplicationCommandAutocompleteResult: 8,
	Modal: 9,
} as const;

/** Message flags relevant to this bot. IS_COMPONENTS_V2 disables `content`,
 *  `embeds`, `poll` and `stickers` on the message that carries it. */
export const MessageFlags = {
	Ephemeral: 1 << 6,
	IsComponentsV2: 1 << 15,
} as const;

export interface ApplicationCommandOption {
	name: string;
	description: string;
	/** Discord's ApplicationCommandOptionType: 1 = SUB_COMMAND, 3 = STRING,
	 *  4 = INTEGER, 5 = BOOLEAN, 6 = USER, 10 = NUMBER. */
	type: number;
	required?: boolean;
	/** INTEGER and NUMBER options carry numeric choice values, not strings. */
	choices?: { name: string; value: string | number }[];
	min_value?: number;
	max_value?: number;
	/** A SUB_COMMAND (type 1) nests its own options. `/twin approach …` is one,
	 *  which is why this is not a flat list. */
	options?: ApplicationCommandOption[];
}

export interface CommandDefinition {
	name: string;
	description: string;
	/** ApplicationCommandType: 1 = CHAT_INPUT. */
	type: 1;
	options?: ApplicationCommandOption[];
	/** Permission bitfield as a string, or "0" to hide from everyone without
	 *  guild administrator rights. A client-side gate only — handlers that
	 *  need this to actually be maintainer-only check the invoker as well. */
	default_member_permissions?: string;
}

/** One submitted modal component. Discord's MODAL_SUBMIT payload sends these
 *  flat in the newer Label form (see src/modal.ts) but still nested one
 *  Action-Row deep in the older form, so `components` is optional rather than
 *  absent — `modalValue` walks both shapes. */
export interface SubmittedComponent {
	type?: number;
	custom_id?: string;
	value?: unknown;
	values?: unknown;
	components?: SubmittedComponent[];
}

export interface InteractionData {
	name?: string;
	custom_id?: string;
	/** `value` is whatever type the option was declared as — STRING, INTEGER,
	 *  NUMBER and BOOLEAN options all arrive here — and a SUB_COMMAND carries
	 *  nested `options` instead of a value. `optionString` narrows before use;
	 *  nothing should read `.value` and assume a string. */
	options?: {
		name: string;
		value?: string | number | boolean;
		options?: InteractionData["options"];
	}[];
	components?: SubmittedComponent[];
}

export interface InteractionMember {
	user?: { id: string };
}

export interface Interaction {
	id: string;
	type: number;
	token: string;
	application_id: string;
	data?: InteractionData;
	member?: InteractionMember;
	user?: { id: string };
	/** The channel this interaction was sent from (verified against
	 *  docs.discord.com 2026-08-04: Discord sends both `channel_id` and a
	 *  partial `channel` object; only the simpler of the two is used here).
	 *  `/test-result` uses this to find which claim thread it was run in. */
	channel_id?: string;
}

/** Serialises an interaction response body. Discord expects exactly this
 *  shape back within the 3 second deadline, or a defer followed by a PATCH
 *  to the response webhook. */
export function jsonResponse(body: unknown, status = 200): Response {
	return new Response(JSON.stringify(body), {
		status,
		headers: { "content-type": "application/json" },
	});
}

/**
 * A CHANNEL_MESSAGE_WITH_SOURCE response, **ephemeral unless told otherwise**.
 *
 * The default is deliberate and is the one place the two halves of this bot
 * disagreed: triage replies defaulted to ephemeral, compatibility replies to
 * public. Nothing in the type system can catch that — it is a runtime default
 * behind ~50 call sites — so it resolves in the fail-safe direction. A reply
 * that should have been public and came out private is a visible annoyance
 * somebody reports; a reply that should have been private and came out public
 * has already been read by the channel. Say `{ ephemeral: false }`, or use
 * `publicMessage`, to opt a reply into being visible to everyone.
 *
 * Mention parsing is suppressed unconditionally: nothing this bot echoes back
 * should ever be able to ping, since some of what it echoes is user-typed.
 * `onlyUsers` is the single sanctioned exception.
 */
export function message(content: string, opts: { ephemeral?: boolean } = {}): Response {
	const ephemeral = opts.ephemeral !== false;
	return jsonResponse({
		type: InteractionResponseType.ChannelMessageWithSource,
		data: {
			content,
			...(ephemeral ? { flags: MessageFlags.Ephemeral } : {}),
			allowed_mentions: NO_MENTIONS,
		},
	});
}

/** A reply everyone in the channel can see. Exists so that "this is public"
 *  is written at the call site rather than inferred from an absent argument,
 *  which is what made the two halves' defaults disagree unnoticed. */
export function publicMessage(content: string): Response {
	return message(content, { ephemeral: false });
}

/** The Discord user ID that invoked an interaction: `member.user.id` in a
 *  guild, `user.id` in a DM. Never a username or display name. */
export function invokerId(interaction: Interaction): string | null {
	return interaction.member?.user?.id ?? interaction.user?.id ?? null;
}

/** A command option's string value, trimmed and capped. Discord enforces its
 *  own limits client-side; this Worker does not trust that. */
export function optionString(interaction: Interaction, name: string, max: number): string | null {
	const raw = interaction.data?.options?.find((o) => o.name === name)?.value;
	if (typeof raw !== "string") return null;
	const trimmed = raw.trim().slice(0, max);
	return trimmed.length > 0 ? trimmed : null;
}

/** A DEFERRED_CHANNEL_MESSAGE_WITH_SOURCE response: the "thinking…"
 *  placeholder for work that will not finish inside the 3 second deadline. */
export function deferredMessage(opts: { ephemeral?: boolean } = {}): Response {
	return jsonResponse({
		type: InteractionResponseType.DeferredChannelMessageWithSource,
		data: { flags: opts.ephemeral ? MessageFlags.Ephemeral : undefined },
	});
}

/** A MODAL response. Must be the immediate response to the interaction —
 *  there is no deferring first and opening one after. */
export function modalResponse(modal: {
	custom_id: string;
	title: string;
	components: unknown[];
}): Response {
	return jsonResponse({ type: InteractionResponseType.Modal, data: modal });
}

/** A DEFERRED_UPDATE_MESSAGE response: for a MESSAGE_COMPONENT interaction
 *  (a button click) whose eventual edit will not finish inside the 3 second
 *  deadline. Unlike `deferredMessage`, this carries no placeholder content —
 *  Discord shows no "thinking…" state at all, it just leaves the existing
 *  message as-is until the follow-up edit lands. */
export function deferredUpdate(): Response {
	return jsonResponse({ type: InteractionResponseType.DeferredUpdateMessage });
}

export function newCorrelationId(): string {
	return crypto.randomUUID();
}

/**
 * Mention parsing off, always. Every string this Worker echoes back has passed
 * through a user field at some point, and an empty `parse` list is the only
 * thing that stops `@everyone` in one of them from becoming a real ping.
 */
export const NO_MENTIONS = { parse: [] as string[] };

/**
 * Suppress everything, then re-allow specific user IDs.
 *
 * The only pings this bot ever sends are to the maintainer, on a no-match or
 * on request, and the ID comes from configuration rather than from a field
 * somebody typed. `parse: []` still holds, so nothing inside the user's own
 * text can become a mention.
 */
export function onlyUsers(ids: string[]): { parse: string[]; users: string[] } {
	return { parse: [], users: ids };
}

export const TextInputStyle = { Short: 1, Paragraph: 2 } as const;

export interface ModalInput {
	customId: string;
	/** Discord caps this at 45 characters. */
	label: string;
	style: number;
	required: boolean;
	maxLength: number;
	placeholder?: string;
}

/** Build and return a modal from plain field descriptions. The ergonomic half
 *  of `modalResponse`, which takes an already-assembled payload. */
export function modal(customId: string, title: string, inputs: ModalInput[]): Response {
	return jsonResponse({
		type: InteractionResponseType.Modal,
		data: {
			custom_id: customId,
			title: title.slice(0, 45),
			components: inputs.map((i) => ({
				type: 1,
				components: [
					{
						type: 4,
						custom_id: i.customId,
						label: i.label.slice(0, 45),
						style: i.style,
						required: i.required,
						max_length: i.maxLength,
						...(i.placeholder ? { placeholder: i.placeholder.slice(0, 100) } : {}),
					},
				],
			})),
		},
	});
}

/** One submitted modal field, trimmed and capped. `null` for absent or empty,
 *  matching `optionString` so callers treat both the same way. */
export function modalValue(interaction: Interaction, customId: string, max: number): string | null {
	for (const row of interaction.data?.components ?? []) {
		for (const field of row.components ?? []) {
			if (field.custom_id === customId && typeof field.value === "string") {
				const trimmed = field.value.trim().slice(0, max);
				return trimmed.length > 0 ? trimmed : null;
			}
		}
	}
	return null;
}
