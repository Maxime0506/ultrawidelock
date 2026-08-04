/**
 * @file Orchestrates the Linked Roles flow across the three plain browser
 * routes (`/linked-role`, `/discord-oauth-callback`, `/github-oauth-callback`)
 * that `src/index.ts` registers alongside — but structurally separate
 * from — the signed interactions endpoint. These are ordinary redirects a
 * browser follows, not Discord interactions: no Ed25519 signature is
 * involved or expected, which is also why they live on their own routes
 * rather than folded into `POST /`.
 *
 * Each exported function takes the incoming request's own URL and derives
 * both OAuth redirect URIs from its origin, so no separate "base URL"
 * secret is needed — the Worker always knows where it is being reached at.
 *
 * Metadata is only ever pushed once, right after both OAuth legs complete.
 * There is no scheduled refresh: re-running `/linked-role` is how a
 * contributor updates their badge later (a periodic full refresh across
 * every linked user is a natural follow-up, not built here, since it would
 * mean spending every linked user's share of GitHub's search rate limit on
 * every sweep tick whether or not anything about them changed).
 */
import type { Env } from "./env.ts";
import { beginDiscordLeg, advanceToGithubLeg, consumeGithubLeg } from "./oauthState.ts";
import {
	discordAuthorizeUrl,
	exchangeDiscordCode,
	getDiscordUserId,
	pushRoleConnection,
} from "./discordOAuth.ts";
import { countMergedPullRequests, exchangeGithubCode, getGithubIdentity, githubAuthorizeUrl } from "./githubOAuth.ts";
import {
	decryptedAccessToken,
	getLink,
	markMetadataPushed,
	saveDiscordAccessToken,
	saveGithubLink,
	scrubLinkSecrets,
} from "./oauthLinks.ts";
import { computeRegistryMetadata, stringifyMetadata } from "./roleConnection.ts";

function htmlPage(title: string, bodyHtml: string, status = 200): Response {
	const html = `<!doctype html><html><head><meta charset="utf-8"><title>${title}</title></head><body>${bodyHtml}</body></html>`;
	return new Response(html, { status, headers: { "content-type": "text/html; charset=utf-8" } });
}

function errorPage(message: string, correlationId: string, status = 400): Response {
	return htmlPage(
		"openaliro compatibility bot",
		`<p>${message}</p><p>Quote <code>${correlationId}</code> if you report this.</p>`,
		status,
	);
}

function callbackUrl(requestUrl: string, path: string): string {
	return new URL(path, requestUrl).toString();
}

export async function startLinkedRole(env: Env, requestUrl: string, correlationId: string): Promise<Response> {
	if (!env.DISCORD_CLIENT_ID) {
		console.error(`[${correlationId}] /linked-role hit with no DISCORD_CLIENT_ID bound`);
		return errorPage("Linked Roles is not configured on this bot yet.", correlationId, 503);
	}

	let state: string;
	try {
		state = await beginDiscordLeg(env.DB);
	} catch (err) {
		console.error(`[${correlationId}] beginDiscordLeg failed:`, err);
		return errorPage("Linked Roles is temporarily unavailable.", correlationId, 503);
	}

	const redirectUri = callbackUrl(requestUrl, "/discord-oauth-callback");
	return Response.redirect(discordAuthorizeUrl(env.DISCORD_CLIENT_ID, redirectUri, state), 302);
}

export async function handleDiscordCallback(env: Env, requestUrl: string, correlationId: string): Promise<Response> {
	const url = new URL(requestUrl);
	const code = url.searchParams.get("code");
	const state = url.searchParams.get("state");
	if (!code || !state) {
		return errorPage("Missing code or state — start over at /linked-role.", correlationId);
	}
	if (!env.DISCORD_CLIENT_ID || !env.DISCORD_CLIENT_SECRET || !env.OAUTH_ENCRYPTION_KEY || !env.GITHUB_CLIENT_ID) {
		console.error(`[${correlationId}] discord-oauth-callback hit with Linked Roles not fully configured`);
		return errorPage("Linked Roles is not configured on this bot yet.", correlationId, 503);
	}

	const tokens = await exchangeDiscordCode(env.DISCORD_CLIENT_ID, env.DISCORD_CLIENT_SECRET, code, callbackUrl(requestUrl, "/discord-oauth-callback"), correlationId);
	if (!tokens) {
		return errorPage("Discord did not authorize this request. Try /linked-role again.", correlationId);
	}

	const discordUserId = await getDiscordUserId(tokens.accessToken, correlationId);
	if (!discordUserId) {
		return errorPage("Could not identify your Discord account. Try /linked-role again.", correlationId);
	}

	// Validate the state — and thus that this callback belongs to a flow
	// this Worker actually started — before persisting anything. A bogus,
	// replayed, or expired state must leave no trace in oauth_links.
	let advanced: boolean;
	try {
		advanced = await advanceToGithubLeg(env.DB, state, discordUserId);
	} catch (err) {
		console.error(`[${correlationId}] advanceToGithubLeg failed:`, err);
		return errorPage("Linked Roles is temporarily unavailable.", correlationId, 503);
	}
	if (!advanced) {
		return errorPage("This link expired or was already used. Start over at /linked-role.", correlationId);
	}

	try {
		await saveDiscordAccessToken(env.DB, discordUserId, tokens.accessToken, env.OAUTH_ENCRYPTION_KEY);
	} catch (err) {
		console.error(`[${correlationId}] saveDiscordAccessToken failed:`, err);
		return errorPage("Could not save your Discord authorization. Try /linked-role again.", correlationId, 503);
	}

	const githubRedirectUri = callbackUrl(requestUrl, "/github-oauth-callback");
	return Response.redirect(githubAuthorizeUrl(env.GITHUB_CLIENT_ID, githubRedirectUri, state), 302);
}

export async function handleGithubCallback(env: Env, requestUrl: string, correlationId: string): Promise<Response> {
	const url = new URL(requestUrl);
	const code = url.searchParams.get("code");
	const state = url.searchParams.get("state");
	if (!code || !state) {
		return errorPage("Missing code or state — start over at /linked-role.", correlationId);
	}
	if (!env.GITHUB_CLIENT_ID || !env.GITHUB_CLIENT_SECRET || !env.DISCORD_CLIENT_ID || !env.DISCORD_CLIENT_SECRET || !env.OAUTH_ENCRYPTION_KEY) {
		console.error(`[${correlationId}] github-oauth-callback hit with Linked Roles not fully configured`);
		return errorPage("Linked Roles is not configured on this bot yet.", correlationId, 503);
	}

	let discordUserId: string | null;
	try {
		discordUserId = await consumeGithubLeg(env.DB, state);
	} catch (err) {
		console.error(`[${correlationId}] consumeGithubLeg failed:`, err);
		return errorPage("Linked Roles is temporarily unavailable.", correlationId, 503);
	}
	if (!discordUserId) {
		return errorPage("This link expired, was already used, or skipped the Discord step. Start over at /linked-role.", correlationId);
	}

	const githubToken = await exchangeGithubCode(env.GITHUB_CLIENT_ID, env.GITHUB_CLIENT_SECRET, code, callbackUrl(requestUrl, "/github-oauth-callback"), correlationId);
	if (!githubToken) {
		return errorPage("GitHub did not authorize this request. Try /linked-role again.", correlationId);
	}

	const identity = await getGithubIdentity(githubToken, correlationId);
	if (!identity) {
		return errorPage("Could not identify your GitHub account. Try /linked-role again.", correlationId);
	}

	try {
		await saveGithubLink(env.DB, discordUserId, identity, Date.now());
	} catch (err) {
		console.error(`[${correlationId}] saveGithubLink failed:`, err);
		return errorPage("Could not save your GitHub link. Try /linked-role again.", correlationId, 503);
	}

	const pushed = await pushMetadataNow(env, discordUserId, identity.githubLogin, correlationId);
	if (!pushed) {
		return htmlPage(
			"openaliro compatibility bot",
			`<p>GitHub account <strong>${identity.githubLogin}</strong> linked, but updating your Discord badge roles failed. ` +
				`Try <a href="/linked-role">/linked-role</a> again — your link itself is saved.</p>`,
		);
	}

	return htmlPage(
		"openaliro compatibility bot",
		`<p>Linked! GitHub account <strong>${identity.githubLogin}</strong> is connected, and your badge roles will reflect ` +
			`the registry the next time Discord checks. You can close this tab.</p>`,
	);
}

/** Computes and pushes the five metadata fields with the access token the
 *  Discord leg parked, then scrubs it. Returns whether the push itself
 *  succeeded — the GitHub link is already saved by this point regardless. */
async function pushMetadataNow(env: Env, discordUserId: string, githubLogin: string, correlationId: string): Promise<boolean> {
	const key = env.OAUTH_ENCRYPTION_KEY;
	const clientId = env.DISCORD_CLIENT_ID;
	const clientSecret = env.DISCORD_CLIENT_SECRET;
	if (!key || !clientId || !clientSecret) return false;

	const link = await getLink(env.DB, discordUserId);
	if (!link) {
		console.error(`[${correlationId}] pushMetadataNow: no oauth_links row for ${discordUserId}`);
		return false;
	}

	const accessToken = await decryptedAccessToken(link, key);
	if (!accessToken) {
		// Already scrubbed, which means this row's push already succeeded and
		// something is replaying the callback. Re-authorising is the only way
		// forward, and it is what /linked-role does.
		console.error(`[${correlationId}] pushMetadataNow: no token on the row for ${discordUserId}`);
		return false;
	}

	const registryMeta = await computeRegistryMetadata(env.DB, discordUserId);
	const mergedPrs = await countMergedPullRequests(githubLogin, env.GITHUB_REPO, env.GITHUB_READ_TOKEN, correlationId);
	const metadata = stringifyMetadata({ ...registryMeta, merged_prs: mergedPrs });

	const ok = await pushRoleConnection(accessToken, clientId, metadata, correlationId);
	if (!ok) return false;

	await markMetadataPushed(env.DB, discordUserId, Date.now());
	// The last thing that needed the tokens and the login has now happened, so
	// they stop existing. Deliberately after markMetadataPushed and not folded
	// into it: a scrub that failed would otherwise be indistinguishable from a
	// push that failed, and the two want different responses.
	try {
		await scrubLinkSecrets(env.DB, discordUserId);
	} catch (err) {
		// The badge is already set; the credential outliving the flow is the
		// part that matters, and the sweep will delete this row on its next tick.
		console.error(`[${correlationId}] scrubLinkSecrets failed for ${discordUserId}:`, err);
	}
	return true;
}
