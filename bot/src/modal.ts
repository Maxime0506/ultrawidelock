/**
 * @file Modal building and parsing.
 *
 * Discord's current modal system wraps each field in a Label (type 18)
 * component placed directly in `data.components` — no Action Row wrapper,
 * which is the older, now-deprecated shape a text-only modal used. Verified
 * against docs.discord.com/developers/interactions/message-components and
 * .../components/reference on 2026-08-04: Label carries `label` and
 * `description`; the wrapped component (Text Input type 4, String Select
 * type 3) carries no label of its own. A submitted modal comes back as a
 * *flat* array of component values — not nested inside the Label — each
 * with `custom_id` and either `value` (text input) or `values` (select).
 *
 * This is a newer, less-travelled part of the API than the rest of this
 * bot's wire handling. It has not been proven against a live Discord round
 * trip; `test/modal.test.ts` proves only that this file's own
 * build/parse pair agrees with itself and with the shapes documented above.
 */

import type { SubmittedComponent } from "./discord.ts";

export const TextInputStyle = { Short: 1, Paragraph: 2 } as const;

export interface TextInputField {
	kind: "text";
	customId: string;
	label: string;
	description?: string;
	style: number;
	required?: boolean;
	minLength?: number;
	maxLength?: number;
	placeholder?: string;
	value?: string;
}

export interface SelectOption {
	name: string;
	value: string;
}

export interface SelectField {
	kind: "select";
	customId: string;
	label: string;
	description?: string;
	options: readonly SelectOption[];
	required?: boolean;
	placeholder?: string;
}

export type ModalField = TextInputField | SelectField;

function buildComponent(field: ModalField): Record<string, unknown> {
	if (field.kind === "text") {
		return {
			type: 4,
			custom_id: field.customId,
			style: field.style,
			required: field.required ?? true,
			min_length: field.minLength,
			max_length: field.maxLength,
			placeholder: field.placeholder?.slice(0, 100),
			value: field.value,
		};
	}
	return {
		type: 3,
		custom_id: field.customId,
		required: field.required ?? true,
		options: field.options.slice(0, 25).map((o) => ({
			label: o.name.slice(0, 100),
			value: o.value.slice(0, 100),
		})),
		placeholder: field.placeholder?.slice(0, 150),
	};
}

/** A modal response body (the `data` half of an interaction response with
 *  `type: 9`). `customId` is where per-invocation state that a ModalSubmit
 *  interaction cannot otherwise recover — e.g. the board this /ihave modal
 *  is for — gets smuggled through. */
export function buildModal(
	customId: string,
	title: string,
	fields: readonly ModalField[],
): { custom_id: string; title: string; components: Record<string, unknown>[] } {
	return {
		custom_id: customId.slice(0, 100),
		title: title.slice(0, 45),
		components: fields.slice(0, 5).map((f) => ({
			type: 18,
			label: f.label.slice(0, 45),
			description: f.description?.slice(0, 100),
			component: buildComponent(f),
		})),
	};
}

/** One submitted text input's value, trimmed, or undefined if absent, empty,
 *  or not the type expected. `components` is the flat array Discord sends on
 *  MODAL_SUBMIT. */
export function textFieldValue(
	components: SubmittedComponent[] | undefined,
	customId: string,
): string | undefined {
	const found = (components ?? []).find((c) => c.custom_id === customId);
	if (!found || typeof found.value !== "string") return undefined;
	const trimmed = found.value.trim();
	return trimmed.length > 0 ? trimmed : undefined;
}

/** One submitted select's chosen value (single-select only: the first of
 *  `values`), or undefined if absent or empty. */
export function selectFieldValue(
	components: SubmittedComponent[] | undefined,
	customId: string,
): string | undefined {
	const found = (components ?? []).find((c) => c.custom_id === customId);
	if (!found || !Array.isArray(found.values)) return undefined;
	const first = found.values[0];
	return typeof first === "string" && first.length > 0 ? first : undefined;
}
