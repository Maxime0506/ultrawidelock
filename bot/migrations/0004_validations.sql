-- Test results, feeding /matrix's ✅/❌ glyphs. One row per test event, not
-- per (board, ios_version) pair: a board can be re-tested (a regression, a
-- later fix), and the history matters even though the matrix only ever
-- shows the latest.
CREATE TABLE IF NOT EXISTS validations (
	id           TEXT    PRIMARY KEY,
	board        TEXT    NOT NULL,
	ios_version  TEXT    NOT NULL,
	passed       INTEGER NOT NULL,
	tested_by    TEXT    NOT NULL,
	request_id   TEXT,
	tested_at    INTEGER NOT NULL
);

-- /matrix's read path: latest row per (board, ios_version).
CREATE INDEX IF NOT EXISTS validations_board_ios_tested_at ON validations (board, ios_version, tested_at);
