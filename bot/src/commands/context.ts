/**
 * @file `/context` — the firmware-side `make doctor` the tree does not have.
 *
 * The HA agent has a `doctor` step (`docs/home-assistant.md:120`); nothing
 * equivalent exists for the firmware itself. This is not that tool, and it
 * does not pretend to be: it cannot inspect a contributor's machine from a
 * Cloudflare Worker. What it can do is print the one fact this repository
 * pins (the NCS version) next to what a report is missing, as one block
 * ready to paste, so asking for it is not a fourth round trip.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { boardLabel } from "../boards.ts";
import { invokerId, message } from "../discord.ts";
import { entriesForUser, RegistryUnavailable } from "../rigs.ts";
import { defer } from "../followup.ts";

/** `Makefile:42` — `NCS_VER ?= v3.3.0`. Both Zephyr ports build against this. */
const NCS_PIN = "v3.3.0";
const NCS_PIN_CITATION = "`Makefile:42`";

export const definition: CommandDefinition = {
	name: "context",
	description: "A paste-able block: your registered hardware, plus what a report still needs",
	type: 1,
};

export function handler(c: CommandContext): Response {
	const userId = invokerId(c.interaction);
	if (!userId) {
		return message("Could not tell who invoked that.");
	}

	return defer(c, async () => {
		let hardware: string[] = [];
		let registryLost = false;
		try {
			const rows = await entriesForUser(c.env.DB, userId);
			hardware = rows.map((r) =>
				[
					`**${boardLabel(r.board)}**`,
					r.phone_model,
					r.ios_version ? `iOS ${r.ios_version}` : null,
				]
					.filter(Boolean)
					.join(" · "),
			);
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] registry read failed:`, err.cause ?? err);
				registryLost = true;
			} else {
				throw err;
			}
		}

		return block(hardware, registryLost);
	});
}

export function block(hardware: string[], registryLost: boolean): string {
	const lines = ["```", "-- context --"];

	if (registryLost) {
		lines.push("registered hardware: (registry unreachable, try /ihave again later)");
	} else if (hardware.length === 0) {
		lines.push("registered hardware: none (/ihave to register a board)");
	} else {
		lines.push("registered hardware:");
		for (const h of hardware) lines.push(`  - ${h.replace(/\*\*/g, "")}`);
	}

	lines.push(
		"",
		`repo NCS pin: ${NCS_PIN}`,
		"host OS: <fill in>",
		"NCS version installed (nrfutil sdk-manager toolchain list): <fill in>",
		"firmware commit (git rev-parse --short HEAD): <fill in>",
		"```",
		`-# repo pin from ${NCS_PIN_CITATION}. This is not a live doctor: it cannot see your` +
			" machine, only what this repository expects. Fill in the three blanks before pasting.",
	);

	return lines.join("\n");
}
