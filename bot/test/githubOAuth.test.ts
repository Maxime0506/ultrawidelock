import { test } from "node:test";
import assert from "node:assert/strict";
import { countMergedPullRequests, exchangeGithubCode, getGithubIdentity, githubAuthorizeUrl } from "../src/githubOAuth.ts";

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

test("githubAuthorizeUrl requests no scope at all — only public account identity is needed", () => {
	const url = githubAuthorizeUrl("gh-client", "https://bot.example/github-oauth-callback", "state-1");
	const parsed = new URL(url);
	assert.equal(parsed.origin + parsed.pathname, "https://github.com/login/oauth/authorize");
	assert.equal(parsed.searchParams.get("client_id"), "gh-client");
	assert.equal(parsed.searchParams.get("redirect_uri"), "https://bot.example/github-oauth-callback");
	assert.equal(parsed.searchParams.get("state"), "state-1");
	assert.equal(parsed.searchParams.has("scope"), false);
});

test("exchangeGithubCode posts form-encoded and asks for a JSON response", async () => {
	const mock = mockFetch((url, init) => {
		assert.equal(url, "https://github.com/login/oauth/access_token");
		assert.equal((init?.headers as Record<string, string>).accept, "application/json");
		return new Response(JSON.stringify({ access_token: "gho_abc", scope: "", token_type: "bearer" }), { status: 200 });
	});
	try {
		const token = await exchangeGithubCode("cid", "csecret", "code-1", "https://bot.example/cb", "corr-1");
		assert.equal(token, "gho_abc");
		const body = new URLSearchParams(String(mock.calls[0]!.init?.body));
		assert.equal(body.get("client_id"), "cid");
		assert.equal(body.get("code"), "code-1");
	} finally {
		mock.restore();
	}
});

test("exchangeGithubCode returns null when GitHub reports an error in a 200 response", async () => {
	const mock = mockFetch(() => new Response(JSON.stringify({ error: "bad_verification_code" }), { status: 200 }));
	try {
		assert.equal(await exchangeGithubCode("cid", "csecret", "bad-code", "https://bot.example/cb", "corr-1"), null);
	} finally {
		mock.restore();
	}
});

test("getGithubIdentity returns the stable id and login, not any other profile field", async () => {
	const mock = mockFetch((url, init) => {
		assert.equal(url, "https://api.github.com/user");
		assert.equal((init?.headers as Record<string, string>).authorization, "Bearer gho_abc");
		assert.ok((init?.headers as Record<string, string>)["user-agent"], "GitHub requires a User-Agent header");
		return new Response(JSON.stringify({ id: 42, login: "octocat", email: "should-not-be-read@example.com" }), { status: 200 });
	});
	try {
		assert.deepEqual(await getGithubIdentity("gho_abc", "corr-1"), { githubId: 42, githubLogin: "octocat" });
	} finally {
		mock.restore();
	}
});

test("countMergedPullRequests is 0 with no repo configured, and never calls the network", async () => {
	const mock = mockFetch(() => {
		throw new Error("should not be called");
	});
	try {
		assert.equal(await countMergedPullRequests("octocat", undefined, undefined, "corr-1"), 0);
	} finally {
		mock.restore();
	}
});

test("countMergedPullRequests queries the search API scoped to the repo and author, and returns total_count", async () => {
	const mock = mockFetch((url, init) => {
		const parsed = new URL(url);
		assert.equal(parsed.origin + parsed.pathname, "https://api.github.com/search/issues");
		assert.equal(parsed.searchParams.get("q"), "repo:openaliro/openaliro type:pr is:merged author:octocat");
		assert.equal((init?.headers as Record<string, string>).authorization, "Bearer gh-token");
		return new Response(JSON.stringify({ total_count: 7 }), { status: 200 });
	});
	try {
		assert.equal(await countMergedPullRequests("octocat", "openaliro/openaliro", "gh-token", "corr-1"), 7);
	} finally {
		mock.restore();
	}
});

test("countMergedPullRequests degrades to 0 on a failed search rather than throwing", async () => {
	const mock = mockFetch(() => new Response("rate limited", { status: 403 }));
	try {
		assert.equal(await countMergedPullRequests("octocat", "openaliro/openaliro", undefined, "corr-1"), 0);
	} finally {
		mock.restore();
	}
});
