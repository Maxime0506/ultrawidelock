-- Per-user /matrix rate limit. PNG rendering (Satori + WASM SVG rasterizing)
-- is the expensive path the spec calls out by name; the text fallback is
-- cheap enough not to need this, so a cooldown hit degrades to text rather
-- than refusing to answer.
CREATE TABLE IF NOT EXISTS matrix_cooldowns (
	discord_user_id TEXT PRIMARY KEY,
	last_render_at  INTEGER NOT NULL
);
