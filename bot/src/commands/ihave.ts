/**
 * @file `/ihave` — register hardware.
 *
 * Board, radio and NFC front-end are command options: Discord fills these in
 * from `choices` before the interaction ever reaches this Worker, which is a
 * client-validated dropdown exactly like a modal string select would be, and
 * costs nothing against a modal's (unconfirmed) component limit. See
 * bot/README.md for why those three fields live here and not in the modal.
 *
 * Everything else — phone model, iOS version, the awake window, and the UTC
 * offset — is free text or needs a 25-option select the command-option
 * `choices` list cannot hold (iOS version) or is genuinely one-shot text
 * (the awake window), so it goes in a modal. The three option values are not
 * available on the follow-up MODAL_SUBMIT interaction — it is a separate
 * interaction — so they are smuggled through the modal's own `custom_id`.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId, message, modalResponse, optionString } from "../discord.ts";
import {
	BOARDS,
	NFC_FRONT_ENDS,
	RADIOS,
	UTC_OFFSETS,
	isKnownBoard,
	isKnownNfc,
	isKnownRadio,
	isKnownUtcOffsetMinutes,
	isValidIosVersion,
	parseAwakeWindow,
} from "../boards.ts";
import { buildModal, selectFieldValue, textFieldValue, TextInputStyle } from "../modal.ts";
import { RegistryUnavailable, upsertRig } from "../rigs.ts";
import { defer } from "../followup.ts";

const CUSTOM_ID_PREFIX = "ihave";
const PHONE_FIELD = "phone_model";
const IOS_FIELD = "ios_version";
const AWAKE_FIELD = "awake_window";
const UTC_FIELD = "utc_offset";

const MAX_PHONE = 64;

export const definition: CommandDefinition = {
	name: "ihave",
	description: "Register a board you can test on",
	type: 1,
	options: [
		{
			name: "board",
			description: "Which board you have",
			type: 3,
			required: true,
			choices: BOARDS.map((b) => ({ name: b.name, value: b.value })),
		},
		{
			name: "radio",
			description: "Which UWB radio it carries",
			type: 3,
			required: true,
			choices: RADIOS.map((r) => ({ name: r.name, value: r.value })),
		},
		{
			name: "nfc",
			description: "NFC front-end, if any",
			type: 3,
			required: true,
			choices: NFC_FRONT_ENDS.map((n) => ({ name: n.name, value: n.value })),
		},
	],
};

/** `ihave:board:radio:nfc` — none of the three values can contain `:`, they
 *  are closed enums validated before this is built. */
function encodeState(board: string, radio: string, nfc: string): string {
	return `${CUSTOM_ID_PREFIX}:${board}:${radio}:${nfc}`;
}

function decodeState(customId: string): { board: string; radio: string; nfc: string } | null {
	const parts = customId.split(":");
	if (parts.length !== 4 || parts[0] !== CUSTOM_ID_PREFIX) return null;
	const [, board, radio, nfc] = parts;
	if (!board || !radio || !nfc) return null;
	if (!isKnownBoard(board) || !isKnownRadio(radio) || !isKnownNfc(nfc)) return null;
	return { board, radio, nfc };
}

export function handler(c: CommandContext): Response {
	const board = optionString(c.interaction, "board", 32);
	const radio = optionString(c.interaction, "radio", 32);
	const nfc = optionString(c.interaction, "nfc", 32);

	// Discord validates choices, so reaching here means a stale client or a
	// crafted payload. Neither gets a modal opened for it.
	if (!board || !radio || !nfc || !isKnownBoard(board) || !isKnownRadio(radio) || !isKnownNfc(nfc)) {
		return message("That is not a board, radio or NFC option this registry knows about.");
	}

	const modal = buildModal(encodeState(board, radio, nfc), "Register your hardware", [
		{
			kind: "text",
			customId: PHONE_FIELD,
			label: "Phone model",
			description: "e.g. iPhone 15 Pro",
			style: TextInputStyle.Short,
			required: true,
			maxLength: MAX_PHONE,
		},
		{
			kind: "text",
			customId: IOS_FIELD,
			label: "iOS version",
			description: "e.g. 19.1 or 19.1.2",
			style: TextInputStyle.Short,
			required: true,
			maxLength: 16,
			placeholder: "19.1",
		},
		{
			kind: "text",
			customId: AWAKE_FIELD,
			label: "Awake window (local, 24h)",
			description: "e.g. 8-23",
			style: TextInputStyle.Short,
			required: true,
			maxLength: 5,
			placeholder: "8-23",
		},
		{
			kind: "select",
			customId: UTC_FIELD,
			label: "Timezone (UTC offset)",
			options: UTC_OFFSETS.map((o) => ({ name: o.name, value: o.value })),
			required: true,
		},
	]);

	return modalResponse(modal);
}

export function modalHandler(c: CommandContext): Response {
	const userId = invokerId(c.interaction);
	if (!userId) {
		return message("Could not tell who submitted that, so nothing was stored.");
	}

	const state = decodeState(c.interaction.data?.custom_id ?? "");
	if (!state) {
		return message("That form has expired or was tampered with. Run `/ihave` again.");
	}

	const components = c.interaction.data?.components;
	const phoneModel = textFieldValue(components, PHONE_FIELD)?.slice(0, MAX_PHONE) ?? null;

	const iosVersion = textFieldValue(components, IOS_FIELD);
	if (!iosVersion || !isValidIosVersion(iosVersion)) {
		return message('iOS version has to look like "19.1" or "19.1.2". Nothing was stored.');
	}

	const awakeRaw = textFieldValue(components, AWAKE_FIELD);
	const awake = awakeRaw ? parseAwakeWindow(awakeRaw) : null;
	if (!awake) {
		return message('Awake window has to look like "8-23", two hours 0-23. Nothing was stored.');
	}

	const utcRaw = selectFieldValue(components, UTC_FIELD);
	const utcOffset = utcRaw ? Number(utcRaw) : NaN;
	if (!utcRaw || !Number.isInteger(utcOffset) || !isKnownUtcOffsetMinutes(utcOffset)) {
		return message("That timezone was not one of the offered offsets. Nothing was stored.");
	}

	return defer(c, async () => {
		try {
			await upsertRig(c.env.DB, {
				discord_user_id: userId,
				board: state.board,
				radio: state.radio,
				nfc: state.nfc,
				phone_model: phoneModel,
				ios_version: iosVersion,
				utc_offset: utcOffset,
				awake_start: awake.start,
				awake_end: awake.end,
			});
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] registry write failed:`, err.cause ?? err);
				return (
					`The registry is not reachable right now, so that was not stored. ` +
					`Try again in a few minutes. Quote \`${c.correlationId}\` if it keeps failing.`
				);
			}
			throw err;
		}

		return (
			`Registered **${state.board}** (${state.radio}, NFC ${state.nfc}), iOS ${iosVersion}, ` +
			`awake ${awake.start}-${awake.end} local. Running \`/ihave\` again for the same board ` +
			`replaces this entry; \`/forget\` deletes it.`
		);
	});
}

// Declared here rather than in a table in commands/index.ts, so that a modal
// handler cannot exist with nothing routing to it. See command.ts.
export const modalPrefix = "ihave";
export const onModalSubmit = modalHandler;
