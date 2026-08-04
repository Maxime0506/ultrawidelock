import { test } from "node:test";
import assert from "node:assert/strict";
import {
	discordAuthorizeUrl,
	exchangeDiscordCode,
	getDiscordUserId,
	pushRoleConnection,
} from "../src/discordOAuth.ts";

/** A minimal fetch mock local to this file: OAuth calls have a different
 *  shape (form-urlencoded token requests, bearer-authorized JSON GET/PUT)
 *  from the webhook follow-ups test/helpers.ts's captureFollowups models,
 *  so a dedicated mock is clearer than bending that one to fit. */
function mockFetch(responder: (url: string, init: RequestInit | undefined) => Response) {
	const original = globalThis.fetch;
	const calls: { url: string; init: RequestInit | undefined }[] = [];
	globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
		const url = String(input);
		calls.push({ url, init });
		return responder(url, init);
	}) as typeof fetch;
	return { calls, restore: () => void (globalThis.fetch = original) };
}

test("discordAuthorizeUrl includes identify + role_connections.write, the client id, redirect and state", () => {
	const url = discordAuthorizeUrl("client-1", "https://bot.example/discord-oauth-callback", "state-1");
	const parsed = new URL(url);
	assert.equal(parsed.origin + parsed.pathname, "https://discord.com/oauth2/authorize");
	assert.equal(parsed.searchParams.get("response_type"), "code");
	assert.equal(parsed.searchParams.get("client_id"), "client-1");
	assert.equal(parsed.searchParams.get("scope"), "identify role_connections.write");
	assert.equal(parsed.searchParams.get("redirect_uri"), "https://bot.example/discord-oauth-callback");
	assert.equal(parsed.searchParams.get("state"), "state-1");
});

test("exchangeDiscordCode POSTs the authorization_code grant form-encoded and computes an absolute expiry", async () => {
	const mock = mockFetch((url) => {
		assert.equal(url, "https://discord.com/api/v10/oauth2/token");
		return new Response(JSON.stringify({ access_token: "acc", refresh_token: "ref", expires_in: 600 }), { status: 200 });
	});
	try {
		const tokens = await exchangeDiscordCode("cid", "csecret", "the-code", "https://bot.example/cb", "corr-1", 1_000_000);
		assert.deepEqual(tokens, { accessToken: "acc", refreshToken: "ref", expiresAt: 1_000_000 + 600_000 });

		const body = new URLSearchParams(String(mock.calls[0]!.init?.body));
		assert.equal(body.get("grant_type"), "authorization_code");
		assert.equal(body.get("code"), "the-code");
		assert.equal(body.get("redirect_uri"), "https://bot.example/cb");
		assert.equal(body.get("client_id"), "cid");
		assert.equal(body.get("client_secret"), "csecret");
	} finally {
		mock.restore();
	}
});

test("exchangeDiscordCode returns null (not a throw) on a non-2xx response", async () => {
	const mock = mockFetch(() => new Response("bad request", { status: 400 }));
	try {
		assert.equal(await exchangeDiscordCode("cid", "csecret", "code", "https://bot.example/cb", "corr-1"), null);
	} finally {
		mock.restore();
	}
});


test("getDiscordUserId sends the bearer token and returns only the ID", async () => {
	const mock = mockFetch((url, init) => {
		assert.equal(url, "https://discord.com/api/v10/users/@me");
		assert.equal((init?.headers as Record<string, string>).authorization, "Bearer acc-1");
		return new Response(JSON.stringify({ id: "123456789", username: "should-not-be-read" }), { status: 200 });
	});
	try {
		assert.equal(await getDiscordUserId("acc-1", "corr-1"), "123456789");
	} finally {
		mock.restore();
	}
});

test("getDiscordUserId returns null on failure rather than throwing", async () => {
	const mock = mockFetch(() => new Response("unauthorized", { status: 401 }));
	try {
		assert.equal(await getDiscordUserId("bad-token", "corr-1"), null);
	} finally {
		mock.restore();
	}
});

test("pushRoleConnection PUTs the stringified metadata with the user's bearer token", async () => {
	const mock = mockFetch((url, init) => {
		assert.equal(url, "https://discord.com/api/v10/users/@me/applications/app-1/role-connection");
		assert.equal(init?.method, "PUT");
		assert.equal((init?.headers as Record<string, string>).authorization, "Bearer acc-1");
		const body = JSON.parse(String(init?.body));
		assert.deepEqual(body.metadata, { boards_owned: "2" });
		return new Response("{}", { status: 200 });
	});
	try {
		const ok = await pushRoleConnection("acc-1", "app-1", { boards_owned: "2" }, "corr-1");
		assert.equal(ok, true);
	} finally {
		mock.restore();
	}
});

test("pushRoleConnection returns false on failure rather than throwing", async () => {
	const mock = mockFetch(() => new Response("server error", { status: 500 }));
	try {
		assert.equal(await pushRoleConnection("acc-1", "app-1", {}, "corr-1"), false);
	} finally {
		mock.restore();
	}
});
