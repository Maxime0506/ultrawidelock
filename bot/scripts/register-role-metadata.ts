/**
 * @file Registers this application's Linked Roles metadata schema (a
 * separate, one-time-per-change registration from the slash commands in
 * register-commands.ts — different endpoint, no guild involved, since role
 * connection metadata is scoped to the application, not a guild).
 *
 * Run with DISCORD_APPLICATION_ID and DISCORD_BOT_TOKEN in the environment.
 * Neither belongs in this repository.
 */
import { METADATA_RECORDS } from "../src/roleConnection.ts";

const appId = process.env.DISCORD_APPLICATION_ID;
const token = process.env.DISCORD_BOT_TOKEN;

if (!appId || !token) {
	console.error("Set DISCORD_APPLICATION_ID and DISCORD_BOT_TOKEN before running this.");
	process.exit(1);
}

const res = await fetch(`https://discord.com/api/v10/applications/${appId}/role-connections/metadata`, {
	method: "PUT",
	headers: {
		authorization: `Bot ${token}`,
		"content-type": "application/json",
	},
	body: JSON.stringify(METADATA_RECORDS),
});

if (!res.ok) {
	console.error(`registration failed: ${res.status} ${await res.text()}`);
	process.exit(1);
}

const body = (await res.json()) as unknown[];
console.log(`registered ${body.length} metadata record(s): ${METADATA_RECORDS.map((r) => r.key).join(", ")}`);
