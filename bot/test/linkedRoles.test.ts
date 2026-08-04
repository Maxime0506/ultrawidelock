import { test } from "node:test";
import assert from "node:assert/strict";
import app from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { makeExecutionContext } from "./helpers.ts";
import { makeD1 } from "./d1-sqlite.ts";
import { beginDiscordLeg, advanceToGithubLeg } from "../src/oauthState.ts";

const USER = "222222222222222222";

function randomKey(): string {
	return Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString("base64");
}

function baseEnv(db: unknown, overrides: Partial<Env> = {}): Env {
	return {
		DISCORD_PUBLIC_KEY: "unused-for-these-routes",
		DB: db as Env["DB"],
		DISCORD_CLIENT_ID: "discord-client",
		DISCORD_CLIENT_SECRET: "discord-secret",
		GITHUB_CLIENT_ID: "github-client",
		GITHUB_CLIENT_SECRET: "github-secret",
		OAUTH_ENCRYPTION_KEY: randomKey(),
		...overrides,
	};
}

/** Routes a mocked fetch by exact hostname+pathname; every OAuth call in
 *  this flow hits a different, known endpoint, so dispatch by path is
 *  simpler than trying to model one generic responder. */
function mockExternalFetch(routes: Record<string, (init: RequestInit | undefined) => Response>) {
	const original = globalThis.fetch;
	globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
		const url = new URL(String(input));
		const key = url.origin + url.pathname;
		const handler = routes[key];
		if (!handler) throw new Error(`unmocked fetch: ${key}`);
		return handler(init);
	}) as typeof fetch;
	return { restore: () => void (globalThis.fetch = original) };
}

async function get(env: Env, path: string): Promise<Response> {
	const { ctx } = makeExecutionContext();
	return app.fetch(new Request(`https://bot.example${path}`), env, ctx);
}

test("/linked-role with no DISCORD_CLIENT_ID configured is a clear 503, not a crash", async () => {
	const d1 = makeD1();
	try {
		const env = baseEnv(d1.binding, { DISCORD_CLIENT_ID: undefined });
		const res = await get(env, "/linked-role");
		assert.equal(res.status, 503);
		assert.match(await res.text(), /not configured/);
	} finally {
		d1.close();
	}
});

test("/linked-role redirects to Discord's authorize URL with a fresh state recorded in D1", async () => {
	const d1 = makeD1();
	try {
		const env = baseEnv(d1.binding);
		const res = await get(env, "/linked-role");
		assert.equal(res.status, 302);
		const location = new URL(res.headers.get("location")!);
		assert.equal(location.origin + location.pathname, "https://discord.com/oauth2/authorize");
		assert.equal(location.searchParams.get("client_id"), "discord-client");
		assert.equal(location.searchParams.get("redirect_uri"), "https://bot.example/discord-oauth-callback");

		const state = location.searchParams.get("state")!;
		const rows = d1.rows(`SELECT stage FROM oauth_states WHERE state = ?`, state);
		assert.equal(rows[0]!.stage, "discord");
	} finally {
		d1.close();
	}
});

test("/discord-oauth-callback with no code/state is a clear error, no writes", async () => {
	const d1 = makeD1();
	try {
		const env = baseEnv(d1.binding);
		const res = await get(env, "/discord-oauth-callback");
		assert.equal(res.status, 400);
		assert.match(await res.text(), /Missing code or state/);
	} finally {
		d1.close();
	}
});

test("/discord-oauth-callback on an unknown state refuses rather than proceeding", async () => {
	const d1 = makeD1();
	const mock = mockExternalFetch({
		"https://discord.com/api/v10/oauth2/token": () =>
			new Response(JSON.stringify({ access_token: "acc", refresh_token: "ref", expires_in: 600 }), { status: 200 }),
		"https://discord.com/api/v10/users/@me": () => new Response(JSON.stringify({ id: USER }), { status: 200 }),
	});
	try {
		const env = baseEnv(d1.binding);
		const res = await get(env, "/discord-oauth-callback?code=abc&state=never-began");
		assert.match(await res.text(), /expired or was already used/);
		assert.equal(d1.rows(`SELECT * FROM oauth_links`).length, 0, "nothing saved for a state that was never begun");
	} finally {
		mock.restore();
		d1.close();
	}
});

test("/discord-oauth-callback happy path: saves encrypted tokens, advances the state, redirects to GitHub", async () => {
	const d1 = makeD1();
	const env = baseEnv(d1.binding);
	const state = await beginDiscordLeg(d1.binding as never);
	const mock = mockExternalFetch({
		"https://discord.com/api/v10/oauth2/token": (init) => {
			const body = new URLSearchParams(String(init?.body));
			assert.equal(body.get("grant_type"), "authorization_code");
			return new Response(JSON.stringify({ access_token: "acc-1", refresh_token: "ref-1", expires_in: 600 }), { status: 200 });
		},
		"https://discord.com/api/v10/users/@me": () => new Response(JSON.stringify({ id: USER }), { status: 200 }),
	});
	try {
		const res = await get(env, `/discord-oauth-callback?code=abc&state=${state}`);
		assert.equal(res.status, 302);
		const location = new URL(res.headers.get("location")!);
		assert.equal(location.origin + location.pathname, "https://github.com/login/oauth/authorize");
		assert.equal(location.searchParams.get("state"), state, "the same state carries into the GitHub leg");

		const linkRows = d1.rows(`SELECT discord_access_token_enc FROM oauth_links WHERE discord_user_id = ?`, USER);
		assert.equal(linkRows.length, 1);
		assert.doesNotMatch(String(linkRows[0]!.discord_access_token_enc), /acc-1/, "token stored encrypted, not raw");

		const stateRows = d1.rows(`SELECT stage, discord_user_id FROM oauth_states WHERE state = ?`, state);
		assert.equal(stateRows[0]!.stage, "github");
		assert.equal(stateRows[0]!.discord_user_id, USER);
	} finally {
		mock.restore();
		d1.close();
	}
});

test("/github-oauth-callback happy path: links GitHub, pushes metadata, and shows a success page", async () => {
	const d1 = makeD1();
	const env = baseEnv(d1.binding);
	const discordState = await beginDiscordLeg(d1.binding as never);
	await advanceToGithubLeg(d1.binding as never, discordState, USER);

	// Seed a Discord token as if the Discord leg already ran.
	const { saveDiscordAccessToken } = await import("../src/oauthLinks.ts");
	await saveDiscordAccessToken(d1.binding as never, USER, "acc-1",
		env.OAUTH_ENCRYPTION_KEY!,
	);

	let pushedMetadata: Record<string, string> | undefined;
	const mock = mockExternalFetch({
		"https://github.com/login/oauth/access_token": () =>
			new Response(JSON.stringify({ access_token: "gho_abc", token_type: "bearer" }), { status: 200 }),
		"https://api.github.com/user": () => new Response(JSON.stringify({ id: 42, login: "octocat" }), { status: 200 }),
		"https://discord.com/api/v10/users/@me/applications/discord-client/role-connection": (init) => {
			pushedMetadata = JSON.parse(String(init?.body)).metadata;
			return new Response("{}", { status: 200 });
		},
	});
	try {
		const res = await get(env, `/github-oauth-callback?code=abc&state=${discordState}`);
		assert.equal(res.status, 200);
		const text = await res.text();
		assert.match(text, /Linked!/);
		assert.match(text, /octocat/);

		const linkRows = d1.rows(
			`SELECT github_id, github_login, metadata_pushed_at, discord_access_token_enc,
			        token_written_at
			 FROM oauth_links WHERE discord_user_id = ?`,
			USER,
		);
		assert.equal(linkRows[0]!.github_id, 42);
		assert.ok(linkRows[0]!.metadata_pushed_at, "metadata_pushed_at was stamped");
		// The push is the last thing that needed any of this, so a completed
		// link leaves nothing behind worth stealing. The success page above
		// still shows the login; it just is not kept.
		assert.equal(linkRows[0]!.github_login, null, "the username is scrubbed");
		assert.equal(linkRows[0]!.discord_access_token_enc, null, "the access token is scrubbed");
		assert.equal(linkRows[0]!.token_written_at, null);

		assert.ok(pushedMetadata);
		assert.equal(pushedMetadata.boards_owned, "0", "no rigs registered in this test");
		assert.equal(pushedMetadata.merged_prs, "0", "no GITHUB_REPO configured");
	} finally {
		mock.restore();
		d1.close();
	}
});

test("/github-oauth-callback: a failed role-connection push still keeps the GitHub link saved, with a partial-success page", async () => {
	const d1 = makeD1();
	const env = baseEnv(d1.binding);
	const discordState = await beginDiscordLeg(d1.binding as never);
	await advanceToGithubLeg(d1.binding as never, discordState, USER);
	const { saveDiscordAccessToken } = await import("../src/oauthLinks.ts");
	await saveDiscordAccessToken(d1.binding as never, USER, "acc-1",
		env.OAUTH_ENCRYPTION_KEY!,
	);

	const mock = mockExternalFetch({
		"https://github.com/login/oauth/access_token": () =>
			new Response(JSON.stringify({ access_token: "gho_abc", token_type: "bearer" }), { status: 200 }),
		"https://api.github.com/user": () => new Response(JSON.stringify({ id: 42, login: "octocat" }), { status: 200 }),
		"https://discord.com/api/v10/users/@me/applications/discord-client/role-connection": () =>
			new Response("server error", { status: 500 }),
	});
	try {
		const res = await get(env, `/github-oauth-callback?code=abc&state=${discordState}`);
		assert.equal(res.status, 200);
		assert.match(await res.text(), /badge roles failed/);

		const linkRows = d1.rows(`SELECT github_login, metadata_pushed_at FROM oauth_links WHERE discord_user_id = ?`, USER);
		assert.equal(linkRows[0]!.github_login, "octocat", "the link itself is still saved");
		assert.equal(linkRows[0]!.metadata_pushed_at, null);
	} finally {
		mock.restore();
		d1.close();
	}
});

test("/github-oauth-callback on a state that never completed the Discord leg refuses", async () => {
	const d1 = makeD1();
	try {
		const env = baseEnv(d1.binding);
		const state = await beginDiscordLeg(d1.binding as never); // never advanced
		const res = await get(env, `/github-oauth-callback?code=abc&state=${state}`);
		assert.match(await res.text(), /skipped the Discord step/);
	} finally {
		d1.close();
	}
});
