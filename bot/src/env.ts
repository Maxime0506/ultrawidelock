/**
 * @file Worker bindings.
 *
 * Every secret here is set with `wrangler secret put NAME`. None of them is
 * ever committed, logged, or echoed into a response. See bot/README.md for the
 * full list and how to set it.
 *
 * One Worker serves both halves of this bot — firmware triage and hardware
 * compatibility tracking — so this is the union of what both need. Anything
 * optional degrades the one feature that reads it rather than failing the
 * Worker, which is what lets `npm test` and a fresh clone run with nothing set.
 */
export interface Env {
	/** The application's Ed25519 public key, hex, from the Discord developer
	 *  portal. Public information, kept as a secret binding only so that the
	 *  repository holds no configuration that identifies the bot. */
	DISCORD_PUBLIC_KEY: string;

	/** The registry. Optional in the type because a missing binding has to
	 *  degrade into "the registry is down" rather than a crash: `/help-me` is
	 *  required to still open a thread when D1 is unreachable. */
	DB?: D1Database;

	/** Discord user IDs allowed to run `/who-has`, separated by commas or
	 *  whitespace. Unset means nobody, which is the safe direction. The first
	 *  is the one `/help-me` pings on a no-match. */
	MAINTAINER_IDS?: string;

	/** Bot token, `Authorization: Bot <token>` on `discord.com/api`. Needed
	 *  only for calls an interaction token cannot make: opening the `/help-me`
	 *  forum thread, posting the `/test-request` Container to a fixed channel
	 *  regardless of where the command was invoked, starting the accept thread,
	 *  and the scheduled escalation edit. Optional in the type for the same
	 *  reason DB is: a missing binding has to degrade a specific feature, not
	 *  crash. Never logged, never echoed. */
	DISCORD_BOT_TOKEN?: string;

	// ---- triage half ----

	/** Where `/help-me` opens threads, as `BOARD=channelId,BOARD=channelId`. */
	FORUM_CHANNELS?: string;

	/** Used for a board with no entry of its own. */
	FORUM_CHANNEL_DEFAULT?: string;

	/** actions:write on this repo only, for `/build`. The single write-scoped
	 *  credential this Worker holds, kept separate from the read-only token
	 *  below precisely so that raising a rate limit never requires handing out
	 *  the ability to start a workflow. Nothing else needs it, and nothing
	 *  else should be given it. */
	GITHUB_ACTIONS_TOKEN?: string;

	// ---- compatibility half ----

	/** The channel `/test-request` posts its Container to — a fixed queue
	 *  rather than wherever the maintainer happened to run the command. */
	TEST_QUEUE_CHANNEL_ID?: string;

	/** Minutes a test request sits with zero accepts before the scheduled
	 *  sweep pings the asleep candidates too. Spec: "a configurable window".
	 *  Unset falls back to a documented default in scheduled.ts rather than
	 *  disabling escalation, since silently never escalating is the wrong
	 *  failure direction for a feature whose entire point is reachability. */
	TEST_REQUEST_ESCALATE_MINUTES?: string;

	/** Discord OAuth2 app credentials, for the Linked Roles flow
	 *  (`/linked-role`). The application ID and OAuth2 client ID are the
	 *  same snowflake in Discord's system, so DISCORD_CLIENT_ID doubles as
	 *  the application ID the role-connection endpoints need. */
	DISCORD_CLIENT_ID?: string;
	DISCORD_CLIENT_SECRET?: string;

	/** GitHub OAuth app credentials — scope-less; only `id`/`login` are ever
	 *  read, per the spec's "verifies the account, not the person". */
	GITHUB_CLIENT_ID?: string;
	GITHUB_CLIENT_SECRET?: string;

	/** "owner/repo" merged PRs are counted against. Unset means merged_prs
	 *  is always 0, the same degrade as a failed search. */
	GITHUB_REPO?: string;

	/** Base64, decodes to exactly 32 bytes: the AES-256-GCM key
	 *  tokenCipher.ts uses to encrypt the Discord OAuth tokens this Worker
	 *  stores in D1 (`oauth_links`) — the one credential this bot persists
	 *  that is not just an opaque Discord user ID. */
	OAUTH_ENCRYPTION_KEY?: string;

	// ---- shared ----

	/** Optional read-only PAT, used purely to raise unauthenticated GitHub API
	 *  rate limits. Two features share one binding because they want the
	 *  identical thing — more headroom on public, read-only endpoints:
	 *  `/verify`'s build-provenance attestation lookup, and the Search API call
	 *  that counts a linked account's merged PRs. Both degrade rather than fail
	 *  without it (`/verify` falls back to the unauthenticated limit,
	 *  `merged_prs` to 0), so it is optional and safe to leave unset. */
	GITHUB_READ_TOKEN?: string;
}
