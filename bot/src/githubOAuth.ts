/**
 * @file The GitHub half of Linked Roles. It verifies the account, not the
 * person, and never learns more than that.
 * In practice that means this only ever asks for the account's public
 * identity: no scope is requested in the authorize URL at all, since
 * `GET /user` returns `id` and `login` for the authenticating account with
 * no scope needed — anything broader would be more access than the feature
 * uses. The resulting token is used once (by the caller, in the OAuth
 * callback) and is never stored; only `id`/`login` persist.
 *
 * Endpoints verified against docs.github.com (2026-08-04).
 */

const USER_AGENT = "openaliro-compat-bot";

export function githubAuthorizeUrl(clientId: string, redirectUri: string, state: string): string {
	const params = new URLSearchParams({ client_id: clientId, redirect_uri: redirectUri, state });
	return `https://github.com/login/oauth/authorize?${params.toString()}`;
}

export async function exchangeGithubCode(
	clientId: string,
	clientSecret: string,
	code: string,
	redirectUri: string,
	correlationId: string,
): Promise<string | null> {
	let res: Response;
	try {
		res = await fetch("https://github.com/login/oauth/access_token", {
			method: "POST",
			headers: { "content-type": "application/x-www-form-urlencoded", accept: "application/json", "user-agent": USER_AGENT },
			body: new URLSearchParams({ client_id: clientId, client_secret: clientSecret, code, redirect_uri: redirectUri }).toString(),
		});
	} catch (err) {
		console.error(`[${correlationId}] GitHub token request threw:`, err);
		return null;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] GitHub token request failed: ${res.status} ${await res.text()}`);
		return null;
	}
	const json = (await res.json()) as { access_token?: string; error?: string };
	if (!json.access_token) {
		console.error(`[${correlationId}] GitHub token response carried no access_token (error: ${json.error ?? "unknown"})`);
		return null;
	}
	return json.access_token;
}

export interface GithubIdentity {
	githubId: number;
	githubLogin: string;
}

/** Used once, at link time, then discarded by the caller — this file never
 *  persists a GitHub token itself. */
export async function getGithubIdentity(accessToken: string, correlationId: string): Promise<GithubIdentity | null> {
	let res: Response;
	try {
		res = await fetch("https://api.github.com/user", {
			headers: { authorization: `Bearer ${accessToken}`, "user-agent": USER_AGENT, accept: "application/vnd.github+json" },
		});
	} catch (err) {
		console.error(`[${correlationId}] GitHub /user threw:`, err);
		return null;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] GitHub /user failed: ${res.status} ${await res.text()}`);
		return null;
	}
	const json = (await res.json()) as { id: number; login: string };
	return { githubId: json.id, githubLogin: json.login };
}

/**
 * Best-effort merged-PR count for the linked account against one
 * configured repo, via GitHub's public Search API. Degrades to 0 on any
 * missing config or failure — of the five metadata fields this is the only
 * one behind a second OAuth account *and* an external search query, so it
 * is the least reliable one and never allowed to block the rest of the
 * push.
 */
export async function countMergedPullRequests(
	githubLogin: string,
	repo: string | undefined,
	githubToken: string | undefined,
	correlationId: string,
): Promise<number> {
	if (!repo) return 0;

	const q = `repo:${repo} type:pr is:merged author:${githubLogin}`;
	const headers: Record<string, string> = { "user-agent": USER_AGENT, accept: "application/vnd.github+json" };
	if (githubToken) headers.authorization = `Bearer ${githubToken}`;

	let res: Response;
	try {
		res = await fetch(`https://api.github.com/search/issues?q=${encodeURIComponent(q)}`, { headers });
	} catch (err) {
		console.error(`[${correlationId}] GitHub search/issues threw:`, err);
		return 0;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] GitHub search/issues failed: ${res.status} ${await res.text()}`);
		return 0;
	}
	const json = (await res.json()) as { total_count?: number };
	return typeof json.total_count === "number" ? json.total_count : 0;
}
