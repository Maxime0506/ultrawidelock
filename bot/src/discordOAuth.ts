/**
 * @file The Discord half of Linked Roles: authorize URL, the two OAuth2
 * token-endpoint grants (authorization_code, refresh_token), identifying
 * who authorized, and pushing the final role-connection metadata. Every
 * endpoint and body shape here was checked against docs.discord.com
 * (2026-08-04) rather than assumed — this is a different trust boundary
 * from the rest of the bot (a real bearer credential granted by an actual
 * user, not just their opaque Discord ID), so it is worth being sure.
 *
 * `identify` is requested alongside `role_connections.write`: the metadata
 * push endpoint is scoped to "whoever this access token belongs to", so the
 * callback needs `GET /users/@me` to learn *which* Discord user just
 * authorized before it can store anything against them.
 */

const API = "https://discord.com/api/v10";
const AUTHORIZE_URL = "https://discord.com/oauth2/authorize";
const SCOPE = "identify role_connections.write";

export function discordAuthorizeUrl(clientId: string, redirectUri: string, state: string): string {
	const params = new URLSearchParams({
		response_type: "code",
		client_id: clientId,
		scope: SCOPE,
		redirect_uri: redirectUri,
		state,
	});
	return `${AUTHORIZE_URL}?${params.toString()}`;
}

export interface DiscordTokens {
	accessToken: string;
	refreshToken: string;
	/** Absolute ms epoch, computed from the response's relative `expires_in`. */
	expiresAt: number;
}

interface TokenResponseBody {
	access_token: string;
	refresh_token: string;
	expires_in: number;
}

async function tokenRequest(
	body: Record<string, string>,
	correlationId: string,
	now: number,
): Promise<DiscordTokens | null> {
	let res: Response;
	try {
		res = await fetch(`${API}/oauth2/token`, {
			method: "POST",
			headers: { "content-type": "application/x-www-form-urlencoded" },
			body: new URLSearchParams(body).toString(),
		});
	} catch (err) {
		console.error(`[${correlationId}] Discord token request threw:`, err);
		return null;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] Discord token request failed: ${res.status} ${await res.text()}`);
		return null;
	}
	const json = (await res.json()) as TokenResponseBody;
	return {
		accessToken: json.access_token,
		refreshToken: json.refresh_token,
		expiresAt: now + json.expires_in * 1000,
	};
}

export function exchangeDiscordCode(
	clientId: string,
	clientSecret: string,
	code: string,
	redirectUri: string,
	correlationId: string,
	now: number = Date.now(),
): Promise<DiscordTokens | null> {
	return tokenRequest(
		{ grant_type: "authorization_code", code, redirect_uri: redirectUri, client_id: clientId, client_secret: clientSecret },
		correlationId,
		now,
	);
}

// There is deliberately no refreshDiscordToken here. The only caller it ever
// had refreshed a token that had expired between the two OAuth legs, which
// cannot happen: the legs are one redirect apart and a Discord grant lasts
// days. Keeping the helper would mean keeping the refresh token that feeds it,
// and a long-lived credential stored against an impossible case is a worse
// trade than re-authorising, which is what /linked-role already does.

/** Who an access token belongs to — never a username, only the ID, matching
 *  every other identity this bot stores. */
export async function getDiscordUserId(accessToken: string, correlationId: string): Promise<string | null> {
	let res: Response;
	try {
		res = await fetch(`${API}/users/@me`, { headers: { authorization: `Bearer ${accessToken}` } });
	} catch (err) {
		console.error(`[${correlationId}] Discord /users/@me threw:`, err);
		return null;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] Discord /users/@me failed: ${res.status} ${await res.text()}`);
		return null;
	}
	const json = (await res.json()) as { id: string };
	return json.id;
}

/** The final step: hands Discord the stringified metadata for whoever
 *  `accessToken` belongs to. Uses the user's own bearer token, not the bot
 *  token — this is a user-scoped endpoint by design. */
export async function pushRoleConnection(
	accessToken: string,
	applicationId: string,
	metadata: Record<string, string>,
	correlationId: string,
): Promise<boolean> {
	let res: Response;
	try {
		res = await fetch(`${API}/users/@me/applications/${applicationId}/role-connection`, {
			method: "PUT",
			headers: { authorization: `Bearer ${accessToken}`, "content-type": "application/json" },
			body: JSON.stringify({ platform_name: "openaliro", metadata }),
		});
	} catch (err) {
		console.error(`[${correlationId}] Discord role-connection push threw:`, err);
		return false;
	}
	if (!res.ok) {
		console.error(`[${correlationId}] Discord role-connection push failed: ${res.status} ${await res.text()}`);
		return false;
	}
	return true;
}
