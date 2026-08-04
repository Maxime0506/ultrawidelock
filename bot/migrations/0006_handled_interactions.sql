-- Delivery deduplication.
--
-- Discord retries an interaction it did not hear back about, and from this
-- Worker's side a retry is indistinguishable from a new submission. Without
-- this table a retried `/help-me` opens a second forum thread with the same
-- report in it, which the maintainer then has to notice and close.
--
-- INSERT OR IGNORE against the primary key is the whole mechanism: the first
-- caller to get a row inserted owns the work.
--
-- Holds no user data. The interaction ID is a snowflake Discord minted for one
-- submission; it names an event, not a person.

CREATE TABLE IF NOT EXISTS handled_interactions (
	interaction_id TEXT    PRIMARY KEY,
	handled_at     INTEGER NOT NULL
);

-- Old rows are worthless once Discord has stopped retrying, which it does
-- within minutes. Nothing prunes them yet; the index is here so that a prune
-- by age stays cheap when there is enough volume to want one.
CREATE INDEX IF NOT EXISTS handled_at ON handled_interactions (handled_at);
