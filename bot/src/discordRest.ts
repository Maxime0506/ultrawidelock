/**
 * @file The handful of Discord REST calls that need a bot token rather than
 * an interaction token: posting to a fixed channel regardless of where a
 * command was invoked, starting a thread, and editing a message outside any
 * live interaction (the scheduled escalation sweep). Everything else in this
 * bot goes through the interaction's own token — see followup.ts — because
 * that requires no secret beyond what Discord itself hands the Worker per
 * request.
 *
 * Every function here fails soft: log and return null/false rather than
 * throw, so a REST hiccup degrades one step of a request-routing flow
 * instead of losing the D1 state already committed around it.
 */

const API = "https://discord.com/api/v10";

export interface PostedMessage {
	id: string;
	channel_id: string;
}

async function call(
	botToken: string,
	correlationId: string,
	path: string,
	init: { method: string; body?: unknown },
): Promise<Response | null> {
	try {
		const res = await fetch(`${API}${path}`, {
			method: init.method,
			headers: {
				authorization: `Bot ${botToken}`,
				"content-type": "application/json",
			},
			body: init.body !== undefined ? JSON.stringify(init.body) : undefined,
		});
		if (!res.ok) {
			console.error(`[${correlationId}] Discord REST ${init.method} ${path} -> ${res.status}: ${await res.text()}`);
			return null;
		}
		return res;
	} catch (err) {
		console.error(`[${correlationId}] Discord REST ${init.method} ${path} threw:`, err);
		return null;
	}
}

/** Posts a message to a channel by ID, independent of any interaction —
 *  what `/test-request` uses to land its Container in the fixed queue
 *  channel rather than wherever the maintainer ran the command. */
export async function postMessage(
	botToken: string,
	correlationId: string,
	channelId: string,
	body: Record<string, unknown>,
): Promise<PostedMessage | null> {
	const res = await call(botToken, correlationId, `/channels/${channelId}/messages`, { method: "POST", body });
	return res ? ((await res.json()) as PostedMessage) : null;
}

/** Edits a channel message by ID outside any live interaction token — the
 *  scheduled escalation sweep is the only caller, since an accept-button
 *  click can (and should) instead edit through its own interaction token
 *  via followup.ts's editOriginalComponents. */
export async function editMessage(
	botToken: string,
	correlationId: string,
	channelId: string,
	messageId: string,
	body: Record<string, unknown>,
): Promise<boolean> {
	const res = await call(botToken, correlationId, `/channels/${channelId}/messages/${messageId}`, {
		method: "PATCH",
		body,
	});
	return res !== null;
}

/** Starts a thread from an existing message. Only GUILD_TEXT / GUILD_ANNOUNCEMENT
 *  channels support this — the test-queue channel is assumed to be one. */
export async function startThreadFromMessage(
	botToken: string,
	correlationId: string,
	channelId: string,
	messageId: string,
	name: string,
): Promise<{ id: string } | null> {
	const res = await call(botToken, correlationId, `/channels/${channelId}/messages/${messageId}/threads`, {
		method: "POST",
		body: { name: name.slice(0, 100) },
	});
	return res ? ((await res.json()) as { id: string }) : null;
}
