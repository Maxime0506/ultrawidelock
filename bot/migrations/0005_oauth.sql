-- Linked Roles: per-user OAuth state, and the only table here that ever
-- touches a real bearer credential.
--
-- Every sensitive column is IN-FLIGHT ONLY, and all of them are nullable so
-- that they can be emptied rather than merely stopped being read. The flow is
-- two browser redirects — Discord authorize, then GitHub authorize — so the
-- access token obtained in the first leg has to survive until the second,
-- which is the entire reason it is written down at all. It is encrypted while
-- it waits (src/tokenCipher.ts, with a Worker secret D1 itself does not hold,
-- since it grants role_connections.write on the holder's behalf), and scrubbed
-- the moment the metadata push that needed it succeeds.
--
-- Discord also hands back a refresh token and an expiry. Neither is stored:
-- the only code that ever read them refreshed a token that had expired between
-- the two legs, which cannot happen when the legs are one redirect apart and
-- the grant lasts days. A long-lived refresh token held against an impossible
-- case is the worst thing this table could contain, so it does not.
--
-- github_login is the same story. It is read once, to count merged pull
-- requests for the badge, and scrubbed with the tokens. A completed link is
-- therefore three columns — who, when, and when the push landed — and nothing
-- a leak would hurt anybody with.
--
-- A flow abandoned between the two legs would otherwise strand live tokens
-- here forever, so the scheduled sweep deletes any row still holding one past
-- the state TTL. That sweep is what makes "in-flight only" true rather than
-- aspirational.
--
-- The GitHub OAuth token is never persisted at all: it is used once, at link
-- time, to call GET /user, then discarded.
CREATE TABLE IF NOT EXISTS oauth_links (
	discord_user_id             TEXT    PRIMARY KEY,
	discord_access_token_enc    TEXT,
	token_written_at            INTEGER,
	github_id                   INTEGER,
	github_login                TEXT,
	linked_at                   INTEGER NOT NULL,
	metadata_pushed_at          INTEGER
);

-- token_written_at exists so the sweep has an honest clock. linked_at cannot
-- serve: it is stamped once and never moved, so a contributor re-running
-- /linked-role months later would have a fresh token on a row that looks old
-- and get purged mid-flow. token_written_at moves every time a token is
-- written and is emptied when it is scrubbed, so "has a token, written before
-- X" is exactly the abandoned set.
CREATE INDEX IF NOT EXISTS oauth_links_inflight ON oauth_links (token_written_at)
	WHERE discord_access_token_enc IS NOT NULL;

-- Chains the two OAuth legs (Discord, then GitHub) into one flow and guards
-- both callbacks against CSRF. `discord_user_id` is filled in once the
-- Discord leg completes, so the GitHub callback knows whose record to
-- finish; short-lived by convention (oauthState.ts checks `created_at`
-- against a TTL at read time rather than a separate sweep, since a stray
-- expired row is harmless and the table is naturally small).
CREATE TABLE IF NOT EXISTS oauth_states (
	state           TEXT    PRIMARY KEY,
	stage           TEXT    NOT NULL,
	discord_user_id TEXT,
	created_at      INTEGER NOT NULL
);
