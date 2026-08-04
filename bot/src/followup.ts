/**
 * @file Deferring, and the edit that finishes a deferred command.
 *
 * Discord gives an interaction 3 seconds. Anything touching D1 defers
 * unconditionally rather than racing that clock, because a command that
 * usually answers in 200 ms and occasionally does not is a command that
 * occasionally fails for no reason a user can see.
 *
 * The interaction token authenticates the follow-up edit, so no bot token is
 * involved here. It expires 15 minutes after the interaction.
 */
import type { CommandContext } from "./command.ts";
import { deferredMessage, deferredUpdate, MessageFlags } from "./discord.ts";

const API = "https://discord.com/api/v10";

/** Answer now, work after. `work` returns the message body to put in place
 *  of the "thinking" placeholder; if it throws, the placeholder is replaced
 *  by an error naming the correlation ID rather than left spinning. */
export function defer(
	c: CommandContext,
	work: (c: CommandContext) => Promise<string>,
	opts: { ephemeral?: boolean } = {},
): Response {
	c.ctx.waitUntil(finish(c, work));
	return deferredMessage(opts.ephemeral === false ? {} : { ephemeral: true });
}

async function finish(c: CommandContext, work: (c: CommandContext) => Promise<string>): Promise<void> {
	let content: string;
	try {
		content = await work(c);
	} catch (err) {
		console.error(`[${c.correlationId}] deferred work threw:`, err);
		content =
			`That failed inside the bot and nothing was changed. Retrying is safe. ` +
			`Quote \`${c.correlationId}\` when reporting it.`;
	}
	await editOriginal(c, content);
}

export interface DeferredResult {
	content: string;
	file?: { bytes: Uint8Array; filename: string };
}

/** Like `defer`, but `work` may also attach a file (an image, so far) to
 *  the follow-up edit. A separate entry point rather than widening `defer`
 *  itself, since every other command only ever needs text. */
export function deferRich(
	c: CommandContext,
	work: (c: CommandContext) => Promise<DeferredResult>,
	opts: { ephemeral?: boolean } = {},
): Response {
	c.ctx.waitUntil(finishRich(c, work));
	return deferredMessage(opts.ephemeral === false ? {} : { ephemeral: true });
}

async function finishRich(c: CommandContext, work: (c: CommandContext) => Promise<DeferredResult>): Promise<void> {
	let result: DeferredResult;
	try {
		result = await work(c);
	} catch (err) {
		console.error(`[${c.correlationId}] deferred work threw:`, err);
		result = {
			content:
				`That failed inside the bot and nothing was changed. Retrying is safe. ` +
				`Quote \`${c.correlationId}\` when reporting it.`,
		};
	}
	await editOriginal(c, result.content, result.file);
}

/** Replace the deferred placeholder, optionally attaching a file. Mentions
 *  stay off either way. */
export async function editOriginal(
	c: CommandContext,
	content: string,
	file?: { bytes: Uint8Array; filename: string },
): Promise<void> {
	const { application_id, token } = c.interaction;
	if (!application_id || !token) {
		console.error(`[${c.correlationId}] cannot edit: interaction carried no token`);
		return;
	}

	const url = `${API}/webhooks/${application_id}/${token}/messages/@original`;
	const payload = { content, allowed_mentions: { parse: [] }, attachments: file ? [{ id: "0", filename: file.filename }] : [] };

	let res: Response;
	if (file) {
		const form = new FormData();
		form.append("payload_json", JSON.stringify(payload));
		form.append("files[0]", new Blob([file.bytes.slice().buffer], { type: "image/png" }), file.filename);
		res = await fetch(url, { method: "PATCH", body: form });
	} else {
		res = await fetch(url, {
			method: "PATCH",
			headers: { "content-type": "application/json" },
			body: JSON.stringify(payload),
		});
	}

	if (!res.ok) {
		console.error(`[${c.correlationId}] follow-up edit failed: ${res.status} ${await res.text()}`);
	}
}

/** Either replace the message a component is attached to (an in-place
 *  status card edit), or leave that message untouched and send a private
 *  note to just the clicker instead — the shape a race-loser's "someone
 *  else already accepted this" response needs, since overwriting the card
 *  with an error would clobber whatever the race winner's edit wrote. */
export type UpdateOutcome = { body: Record<string, unknown> } | { ephemeralNote: string };

/** Like `defer`, but for a MESSAGE_COMPONENT interaction: answers with
 *  DEFERRED_UPDATE_MESSAGE (no "thinking…" placeholder shown) and finishes
 *  by either editing the component's own message or sending a private note. */
export function deferUpdate(c: CommandContext, work: (c: CommandContext) => Promise<UpdateOutcome>): Response {
	c.ctx.waitUntil(finishUpdate(c, work));
	return deferredUpdate();
}

async function finishUpdate(c: CommandContext, work: (c: CommandContext) => Promise<UpdateOutcome>): Promise<void> {
	let outcome: UpdateOutcome;
	try {
		outcome = await work(c);
	} catch (err) {
		console.error(`[${c.correlationId}] deferred update threw:`, err);
		outcome = {
			ephemeralNote: `That failed inside the bot and nothing was changed. Retrying is safe. Quote \`${c.correlationId}\` when reporting it.`,
		};
	}
	if ("ephemeralNote" in outcome) {
		await sendEphemeralFollowup(c, outcome.ephemeralNote);
	} else {
		await editOriginalComponents(c, outcome.body);
	}
}

/** PATCHes @original with an arbitrary components-shaped body, for messages
 *  where `content` is disabled (IS_COMPONENTS_V2) and editOriginal's
 *  content-only shape does not apply. */
export async function editOriginalComponents(c: CommandContext, body: Record<string, unknown>): Promise<void> {
	const { application_id, token } = c.interaction;
	if (!application_id || !token) {
		console.error(`[${c.correlationId}] cannot edit: interaction carried no token`);
		return;
	}
	const res = await fetch(`${API}/webhooks/${application_id}/${token}/messages/@original`, {
		method: "PATCH",
		headers: { "content-type": "application/json" },
		body: JSON.stringify(body),
	});
	if (!res.ok) {
		console.error(`[${c.correlationId}] component follow-up edit failed: ${res.status} ${await res.text()}`);
	}
}

/** POSTs a new ephemeral follow-up message without touching the message the
 *  component is attached to. */
async function sendEphemeralFollowup(c: CommandContext, content: string): Promise<void> {
	const { application_id, token } = c.interaction;
	if (!application_id || !token) {
		console.error(`[${c.correlationId}] cannot send follow-up: interaction carried no token`);
		return;
	}
	const res = await fetch(`${API}/webhooks/${application_id}/${token}`, {
		method: "POST",
		headers: { "content-type": "application/json" },
		body: JSON.stringify({ content, flags: MessageFlags.Ephemeral, allowed_mentions: { parse: [] } }),
	});
	if (!res.ok) {
		console.error(`[${c.correlationId}] ephemeral follow-up failed: ${res.status} ${await res.text()}`);
	}
}
