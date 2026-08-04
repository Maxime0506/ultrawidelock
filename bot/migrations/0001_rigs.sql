-- The hardware registry.
--
-- One row per contributor per board, so somebody with a DWM3001CDK and an
-- nRF5340 DK is two rows and `/forget` with no argument is still one
-- statement. The spec's own table definition showed PRIMARY KEY on
-- discord_user_id alone, which contradicts its prose ("a contributor with
-- three boards has three rows. Composite key") and would silently overwrite
-- a second board registration; the composite key is the one that matches
-- the documented behaviour and is what /ihave, /forget and /who-has assume.
--
-- The only identifier stored is the Discord user ID. No username, no
-- display name, no guild. phone_model, ios_version and probe_serial are
-- free text or pattern-validated text the contributor typed; board, radio
-- and nfc are enums enforced by the application before they ever reach a
-- bound parameter here.
CREATE TABLE IF NOT EXISTS rigs (
	discord_user_id TEXT    NOT NULL,
	board            TEXT    NOT NULL,
	radio            TEXT    NOT NULL,
	nfc              TEXT    NOT NULL,
	phone_model      TEXT,
	ios_version      TEXT,
	probe_serial     TEXT,
	utc_offset       INTEGER NOT NULL,
	awake_start      INTEGER NOT NULL,
	awake_end        INTEGER NOT NULL,
	updated_at       INTEGER NOT NULL,
	PRIMARY KEY (discord_user_id, board)
);

-- /who-has and the future /matrix scan board and ios_version together.
CREATE INDEX IF NOT EXISTS rigs_board ON rigs (board);
CREATE INDEX IF NOT EXISTS rigs_ios ON rigs (ios_version);
