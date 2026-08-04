import { test } from "node:test";
import assert from "node:assert/strict";
import {
	decryptedAccessToken,
	getLink,
	markMetadataPushed,
	purgeAbandonedLinks,
	saveDiscordAccessToken,
	saveGithubLink,
	scrubLinkSecrets,
} from "../src/oauthLinks.ts";
import { brokenD1, makeD1 } from "./d1-sqlite.ts";

const USER = "888888888888888888";

function randomKey(): string {
	return Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString("base64");
}

test("saveDiscordAccessToken stores the token encrypted (not as plaintext) and getLink round-trips them", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "acc-1", key);

		const raw = d1.rows(`SELECT discord_access_token_enc FROM oauth_links WHERE discord_user_id = ?`, USER);
		assert.doesNotMatch(String(raw[0]!.discord_access_token_enc), /acc-1/);

		const row = await getLink(d1.binding as never, USER);
		assert.ok(row);
		assert.equal(await decryptedAccessToken(row, key), "acc-1");
		assert.equal(row.github_id, null, "no GitHub link yet");
	} finally {
		d1.close();
	}
});

test("re-running saveDiscordAccessToken does not clobber an already-linked GitHub identity", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "acc-1", key);
		await saveGithubLink(d1.binding as never, USER, { githubId: 42, githubLogin: "octocat" }, 6_000);

		await saveDiscordAccessToken(d1.binding as never, USER, "acc-2", key);

		const row = await getLink(d1.binding as never, USER);
		assert.equal(row?.github_login, "octocat", "untouched by the token refresh");
		assert.equal(await decryptedAccessToken(row!, key), "acc-2", "the new token, not the old one");
	} finally {
		d1.close();
	}
});

test("saveGithubLink returns false when no Discord-leg row exists yet", async () => {
	const d1 = makeD1();
	try {
		const linked = await saveGithubLink(d1.binding as never, "no-such-user", { githubId: 1, githubLogin: "x" }, 1_000);
		assert.equal(linked, false);
	} finally {
		d1.close();
	}
});

test("markMetadataPushed sets the timestamp", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "a", key);
		await markMetadataPushed(d1.binding as never, USER, 12_345);
		const row = await getLink(d1.binding as never, USER);
		assert.equal(row?.metadata_pushed_at, 12_345);
	} finally {
		d1.close();
	}
});

test("getLink returns null for an unlinked user", async () => {
	const d1 = makeD1();
	try {
		assert.equal(await getLink(d1.binding as never, "nobody"), null);
	} finally {
		d1.close();
	}
});

test("degrades to OAuthLinksUnavailable rather than throwing a raw D1 error", async () => {
	await assert.rejects(
		() => saveDiscordAccessToken(brokenD1() as never, USER, "a", randomKey()),
		{ name: "OAuthLinksUnavailable" },
	);
});

test("scrubLinkSecrets empties every credential column and keeps the record of the link", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "acc", key, 1_000);
		await saveGithubLink(d1.binding as never, USER, { githubId: 42, githubLogin: "octocat" }, 2_000);
		await markMetadataPushed(d1.binding as never, USER, 3_000);

		await scrubLinkSecrets(d1.binding as never, USER);

		const row = await getLink(d1.binding as never, USER);
		assert.ok(row, "the row survives: who linked and when is not a secret");
		// Every column that could identify or impersonate somebody.
		assert.equal(row.discord_access_token_enc, null);
		assert.equal(row.token_written_at, null);
		assert.equal(row.github_login, null, "a username, so it goes");
		// What is deliberately kept.
		assert.equal(row.github_id, 42, "an opaque number a re-push would key on");
		assert.equal(row.linked_at, 2_000);
		assert.equal(row.metadata_pushed_at, 3_000);
	} finally {
		d1.close();
	}
});

test("decryptedAccessToken returns null once scrubbed, rather than throwing", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "a", key, 1_000);
		await scrubLinkSecrets(d1.binding as never, USER);
		const row = await getLink(d1.binding as never, USER);
		assert.equal(await decryptedAccessToken(row!, key), null);
	} finally {
		d1.close();
	}
});

test("purgeAbandonedLinks deletes a stranded flow and spares one that finished", async () => {
	const d1 = makeD1();
	const key = randomKey();
	const ABANDONED = "111111111111111111";
	try {
		// Authorised Discord, then closed the tab: tokens, no scrub.
		await saveDiscordAccessToken(d1.binding as never, ABANDONED, "a", key, 1_000);
		// Completed the flow, so its secrets are already gone.
		await saveDiscordAccessToken(d1.binding as never, USER, "c", key, 1_000);
		await scrubLinkSecrets(d1.binding as never, USER);

		const purged = await purgeAbandonedLinks(d1.binding as never, 2_000);

		assert.equal(purged, 1);
		assert.equal(await getLink(d1.binding as never, ABANDONED), null, "the live token is gone");
		assert.ok(await getLink(d1.binding as never, USER), "a finished link is not collateral");
	} finally {
		d1.close();
	}
});

test("purgeAbandonedLinks spares a flow that is still in its window", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		await saveDiscordAccessToken(d1.binding as never, USER, "a", key, 5_000);
		assert.equal(await purgeAbandonedLinks(d1.binding as never, 4_999), 0, "not yet past the cutoff");
		assert.ok(await getLink(d1.binding as never, USER));
	} finally {
		d1.close();
	}
});

test("saveDiscordAccessToken stamps linked_at and token_written_at with now", async () => {
	const d1 = makeD1();
	const key = randomKey();
	try {
		// linked_at used to share a bound parameter with the token expiry, which
		// put a future timestamp in it and made every in-flight row look newer
		// than it was. The expiry is not stored at all now, but the sweep still
		// depends on these two being a real clock.
		await saveDiscordAccessToken(d1.binding as never, USER, "a", key, 1_000);
		const row = await getLink(d1.binding as never, USER);
		assert.equal(row?.linked_at, 1_000);
		assert.equal(row?.token_written_at, 1_000);
	} finally {
		d1.close();
	}
});
