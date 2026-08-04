-- Per-user cooldown for /build.
--
-- firmware-builds.yml is, in its own words, "the heaviest thing this
-- repository can ask of CI": six jobs, the NCS and ESP-IDF toolchains, tens
-- of minutes each. Of every command this bot offers, dispatching that is the
-- one worth rate limiting hardest.
--
-- One row per user. A dispatch overwrites it rather than accumulating a
-- history; only "when was the last one" is needed to answer "how long until
-- the next one".

CREATE TABLE IF NOT EXISTS build_cooldowns (
	user_id         TEXT    PRIMARY KEY,
	last_dispatch_at INTEGER NOT NULL
);
