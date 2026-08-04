/**
 * @file The two Discord calls that need the bot token.
 *
 * Creating a forum post and posting into a thread are the only things this bot
 * does that an interaction token cannot authorise. The token is a Worker
 * secret, is never logged, and is never interpolated into anything that could
 * be echoed back.
 *
 * Channel IDs are checked against a snowflake pattern before they reach a URL.
 * They come from configuration rather than from a user, but a URL built by
 * concatenation is worth validating whatever the source.
 */
import type { Env } from "./env.ts";

const API = "https://discord.com/api/v10";
const SNOWFLAKE = /^\d{5,25}$/;
const REPO = "openaliro/openaliro";
const WORKFLOW_FILE = "firmware-builds.yml";
const GH_API = "https://api.github.com";

export interface AllowedMentions {
	parse: string[];
	users?: string[];
}

/**
 * The forum channel for a board, or undefined.
 *
 * FORUM_CHANNELS is `BOARD=id,BOARD=id`. An unparseable entry is skipped
 * rather than throwing: a typo in one board's channel must not take
 * `/help-me` down for every other board.
 */
export function forumChannelFor(env: Env, board: string): string | undefined {
	for (const entry of (env.FORUM_CHANNELS ?? "").split(",")) {
		const [name, id] = entry.split("=").map((s) => s.trim());
		if (name === board && id && SNOWFLAKE.test(id)) return id;
	}
	const fallback = (env.FORUM_CHANNEL_DEFAULT ?? "").trim();
	return SNOWFLAKE.test(fallback) ? fallback : undefined;
}

async function call(
	env: Env,
	path: string,
	body: unknown,
	correlationId: string,
): Promise<unknown | null> {
	if (!env.DISCORD_BOT_TOKEN) {
		console.error(`[${correlationId}] DISCORD_BOT_TOKEN is not bound`);
		return null;
	}

	let res: Response;
	try {
		res = await fetch(`${API}${path}`, {
			method: "POST",
			headers: {
				authorization: `Bot ${env.DISCORD_BOT_TOKEN}`,
				"content-type": "application/json",
			},
			body: JSON.stringify(body),
		});
	} catch (err) {
		console.error(`[${correlationId}] ${path} did not complete:`, err);
		return null;
	}

	if (!res.ok) {
		// Discord's error body names the rejected field and carries no credential.
		console.error(`[${correlationId}] ${path} -> ${res.status} ${await res.text()}`);
		return null;
	}

	return res.json();
}

/** Create a forum post. Returns the thread ID, or null if it could not. */
export async function createForumThread(
	env: Env,
	channelId: string,
	name: string,
	content: string,
	allowedMentions: AllowedMentions,
	correlationId: string,
): Promise<string | null> {
	if (!SNOWFLAKE.test(channelId)) {
		console.error(`[${correlationId}] refusing a channel id that is not a snowflake`);
		return null;
	}

	const created = (await call(
		env,
		`/channels/${channelId}/threads`,
		{ name, message: { content, allowed_mentions: allowedMentions } },
		correlationId,
	)) as { id?: string } | null;

	return created?.id ?? null;
}

/** Post a follow-on message into a thread. Best effort. */
export async function postToThread(
	env: Env,
	threadId: string,
	content: string,
	correlationId: string,
): Promise<boolean> {
	if (!SNOWFLAKE.test(threadId)) return false;
	const sent = await call(
		env,
		`/channels/${threadId}/messages`,
		{ content, allowed_mentions: { parse: [] } },
		correlationId,
	);
	return sent !== null;
}

/**
 * Dispatch firmware-builds.yml with a `targets` selection.
 *
 * `workflow_dispatch` itself returns 204 with no run identifier, so the run
 * URL is found by asking for the newest workflow_dispatch run afterward. That
 * is racy against anyone else dispatching the same workflow in the same
 * second; `findLatestRun` accepts the gap and the caller degrades to a runs
 * list link when it cannot find a match.
 *
 * `save_ccache` is always false here. Every per-job ccache entry is keyed on
 * `github.run_id`, so a normal dispatch always writes a fresh ~300 MB cache
 * entry — the right default for a maintainer's own run, wrong for a bot
 * answering a one-off Discord request: ten of those would be 3 GB against the
 * repository's 10 GB Actions cache LRU budget, pressure that can evict
 * nrf-workspace or the esp-matter cache, the two entries actually expensive
 * to rebuild. `workflow_dispatch` inputs are always strings on the wire, so
 * this is the literal string "false", not the boolean.
 */
export async function dispatchFirmwareBuilds(
	env: Env,
	ref: string,
	targets: string,
	correlationId: string,
): Promise<{ ok: boolean; status: number }> {
	const token = env.GITHUB_ACTIONS_TOKEN;
	if (!token) {
		console.error(`[${correlationId}] GITHUB_ACTIONS_TOKEN is not bound`);
		return { ok: false, status: 0 };
	}

	let res: Response;
	try {
		res = await fetch(
			`${GH_API}/repos/${REPO}/actions/workflows/${WORKFLOW_FILE}/dispatches`,
			{
				method: "POST",
				headers: {
					authorization: `Bearer ${token}`,
					accept: "application/vnd.github+json",
					"content-type": "application/json",
					"user-agent": "openaliro-triage-bot",
				},
				body: JSON.stringify({ ref, inputs: { targets, save_ccache: "false" } }),
			},
		);
	} catch (err) {
		console.error(`[${correlationId}] dispatch did not complete:`, err);
		return { ok: false, status: 0 };
	}

	if (!res.ok) {
		console.error(`[${correlationId}] dispatch -> ${res.status} ${await res.text()}`);
	}
	return { ok: res.ok, status: res.status };
}

const RUNS_LIST_URL = `https://github.com/${REPO}/actions/workflows/${WORKFLOW_FILE}`;

/**
 * Poll briefly for the run a dispatch just created. `since` is the time just
 * before the dispatch call, so a run created earlier cannot be mistaken for
 * this one.
 */
export async function findLatestRun(
	env: Env,
	since: number,
	correlationId: string,
): Promise<string> {
	const token = env.GITHUB_ACTIONS_TOKEN ?? env.GITHUB_READ_TOKEN;
	const headers: Record<string, string> = {
		accept: "application/vnd.github+json",
		"user-agent": "openaliro-triage-bot",
	};
	if (token) headers.authorization = `Bearer ${token}`;

	for (let attempt = 0; attempt < 4; attempt++) {
		await new Promise((resolve) => setTimeout(resolve, 1500));
		try {
			const res = await fetch(
				`${GH_API}/repos/${REPO}/actions/workflows/${WORKFLOW_FILE}/runs` +
					`?event=workflow_dispatch&per_page=5`,
				{ headers },
			);
			if (res.ok) {
				const body = (await res.json()) as {
					workflow_runs?: { html_url: string; created_at: string }[];
				};
				const match = (body.workflow_runs ?? []).find(
					(r) => Date.parse(r.created_at) >= since,
				);
				if (match) return match.html_url;
			}
		} catch (err) {
			console.error(`[${correlationId}] run lookup attempt ${attempt} failed:`, err);
		}
	}

	return RUNS_LIST_URL;
}
