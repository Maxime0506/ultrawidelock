/**
 * @file Every statement this Worker runs against `oauth_links`. Same rule
 * as every other D1 module here: no SQL built by concatenation, every
 * value a bound parameter. Token encryption itself lives in
 * tokenCipher.ts — this file only ever handles already-encrypted blobs.
 */
import { decryptToken, encryptToken } from "./tokenCipher.ts";

/** Every credential-bearing column is nullable because it is in-flight only:
 *  present between the two OAuth legs, emptied by `scrubLinkSecrets` as soon as
 *  the push that needed it lands. A row read after a completed link has nulls
 *  in all three. */
export interface OAuthLinkRow {
	discord_user_id: string;
	discord_access_token_enc: string | null;
	token_written_at: number | null;
	github_id: number | null;
	github_login: string | null;
	linked_at: number;
	metadata_pushed_at: number | null;
}

export class OAuthLinksUnavailable extends Error {
	constructor(cause?: unknown) {
		super("oauth links unavailable");
		this.name = "OAuthLinksUnavailable";
		this.cause = cause;
	}
}

function need(db: D1Database | undefined): D1Database {
	if (!db) throw new OAuthLinksUnavailable();
	return db;
}

async function run<T>(fn: () => Promise<T>): Promise<T> {
	try {
		return await fn();
	} catch (err) {
		throw err instanceof OAuthLinksUnavailable ? err : new OAuthLinksUnavailable(err);
	}
}

const SAVE_ACCESS_TOKEN = `
INSERT INTO oauth_links (discord_user_id, discord_access_token_enc, token_written_at, linked_at)
VALUES (?1, ?2, ?3, ?3)
ON CONFLICT (discord_user_id) DO UPDATE SET
	discord_access_token_enc = excluded.discord_access_token_enc,
	token_written_at         = excluded.token_written_at
`;

/**
 * The Discord leg of the flow: parks the encrypted access token where the
 * GitHub leg can pick it up, leaving any already-linked GitHub identity
 * untouched (only the Discord columns are in the UPDATE branch).
 *
 * The access token is the only piece of the grant kept. Discord also returns a
 * refresh token and an expiry, and both used to be stored; the only thing that
 * ever read them was a refresh branch guarding against a token expiring between
 * the two legs, which cannot happen — the legs are one redirect apart and the
 * grant lasts days. Storing a long-lived refresh token to guard an impossible
 * case was the worst trade in this file.
 */
export async function saveDiscordAccessToken(
	db: D1Database | undefined,
	discordUserId: string,
	accessToken: string,
	encryptionKey: string,
	now: number = Date.now(),
): Promise<void> {
	const accessEnc = await encryptToken(accessToken, encryptionKey);
	await run(() => need(db).prepare(SAVE_ACCESS_TOKEN).bind(discordUserId, accessEnc, now).run());
}

const SAVE_GITHUB_LINK = `
UPDATE oauth_links SET github_id = ?2, github_login = ?3, linked_at = ?4 WHERE discord_user_id = ?1
`;

/** The GitHub leg: the row must already exist from `saveDiscordTokens` —
 *  this only ever UPDATEs, never INSERTs, since a GitHub identity with no
 *  Discord tokens behind it is not a state this flow can act on. Returns
 *  whether a row was actually updated. */
export async function saveGithubLink(
	db: D1Database | undefined,
	discordUserId: string,
	github: { githubId: number; githubLogin: string },
	linkedAt: number,
): Promise<boolean> {
	return run(async () => {
		const res = await need(db)
			.prepare(SAVE_GITHUB_LINK)
			.bind(discordUserId, github.githubId, github.githubLogin, linkedAt)
			.run();
		return (res.meta?.changes ?? 0) > 0;
	});
}

const GET_LINK = `
SELECT discord_user_id, discord_access_token_enc, token_written_at,
       github_id, github_login, linked_at, metadata_pushed_at
FROM oauth_links WHERE discord_user_id = ?1
`;

export async function getLink(db: D1Database | undefined, discordUserId: string): Promise<OAuthLinkRow | null> {
	return run(async () => (await need(db).prepare(GET_LINK).bind(discordUserId).first<OAuthLinkRow>()) ?? null);
}

const MARK_METADATA_PUSHED = `UPDATE oauth_links SET metadata_pushed_at = ?2 WHERE discord_user_id = ?1`;

export async function markMetadataPushed(db: D1Database | undefined, discordUserId: string, now: number): Promise<void> {
	await run(() => need(db).prepare(MARK_METADATA_PUSHED).bind(discordUserId, now).run());
}

/** Decrypts the parked access token for a row, or null once it has been
 *  scrubbed. Split out from `getLink` so a caller that only needs to know
 *  *whether* someone is linked (no token material) never has to touch the
 *  cipher at all. */
export async function decryptedAccessToken(row: OAuthLinkRow, encryptionKey: string): Promise<string | null> {
	if (!row.discord_access_token_enc) return null;
	return decryptToken(row.discord_access_token_enc, encryptionKey);
}

const SCRUB = `
UPDATE oauth_links SET
	discord_access_token_enc = NULL,
	token_written_at         = NULL,
	github_login             = NULL
WHERE discord_user_id = ?1
`;

/**
 * Empty every credential-bearing column, leaving who linked and when.
 *
 * Called once the metadata push succeeds, which is the last moment anything
 * needs the token or the GitHub login: Discord keeps the pushed metadata on
 * its own side, and re-running `/linked-role` re-authorises from scratch
 * rather than reusing what is here. `github_id` stays because it is an opaque
 * number that identifies nobody on its own and is what a future re-push would
 * key on; the login, which is a username, does not.
 */
export async function scrubLinkSecrets(db: D1Database | undefined, discordUserId: string): Promise<void> {
	await run(() => need(db).prepare(SCRUB).bind(discordUserId).run());
}

const PURGE_ABANDONED = `
DELETE FROM oauth_links
WHERE discord_access_token_enc IS NOT NULL AND token_written_at < ?1
`;

/**
 * Delete rows abandoned between the two OAuth legs.
 *
 * Somebody who authorises Discord and then closes the tab leaves a live token
 * behind that no completion path will ever scrub. Without this the "in-flight
 * only" claim on the table would hold for every user who finishes and for none
 * who does not, which is the wrong way round: an abandoned flow is exactly the
 * case where nobody is watching. Returns how many rows went.
 */
export async function purgeAbandonedLinks(db: D1Database | undefined, olderThan: number): Promise<number> {
	return run(async () => {
		const res = await need(db).prepare(PURGE_ABANDONED).bind(olderThan).run();
		return res.meta?.changes ?? 0;
	});
}
