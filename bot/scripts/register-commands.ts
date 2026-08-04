/**
 * @file Upload the command list to Discord.
 *
 * Guild-scoped by default because guild registration is instant and global
 * registration lags, which during setup reads as "the bot is broken".
 *
 * Bulk overwrite (PUT), not create: the array in src/commands/index.ts is the
 * whole truth, so a command deleted from the code disappears from Discord on
 * the next run instead of lingering as a slash command with no handler.
 *
 *   DISCORD_APPLICATION_ID=... DISCORD_BOT_TOKEN=... DISCORD_GUILD_ID=... \
 *     npm run register
 *
 * The token is read from the environment, never from a file in the repo, and
 * is never printed, including in error paths.
 */
import { definitions } from "../src/commands/index.ts";

const applicationId = process.env.DISCORD_APPLICATION_ID;
const token = process.env.DISCORD_BOT_TOKEN;
const guildId = process.env.DISCORD_GUILD_ID;

function die(msg: string): never {
	console.error(msg);
	process.exit(1);
}

if (!applicationId) die("DISCORD_APPLICATION_ID is not set.");
if (!token) die("DISCORD_BOT_TOKEN is not set.");

const url = guildId
	? `https://discord.com/api/v10/applications/${applicationId}/guilds/${guildId}/commands`
	: `https://discord.com/api/v10/applications/${applicationId}/commands`;

if (!guildId) {
	console.warn(
		"DISCORD_GUILD_ID is not set, so this registers globally. Global registration\n" +
			"can take up to an hour to appear. Set DISCORD_GUILD_ID for instant registration.",
	);
}

const res = await fetch(url, {
	method: "PUT",
	headers: {
		authorization: `Bot ${token}`,
		"content-type": "application/json",
	},
	body: JSON.stringify(definitions),
});

if (!res.ok) {
	// Discord's error body names the offending field and carries no secret.
	die(`registration failed: ${res.status} ${res.statusText}\n${await res.text()}`);
}

console.log(
	`registered ${definitions.length} command(s) ${guildId ? "to the guild" : "globally"}: ` +
		definitions.map((d) => `/${d.name}`).join(" "),
);
