/**
 * @file `/build <target>` — dispatch firmware-builds.yml.
 *
 * The heaviest thing this bot can ask CI for: up to six jobs, the NCS and
 * ESP-IDF toolchains, tens of minutes. Deferred unconditionally, rate limited
 * harder than anything else here, and idempotent on the interaction ID so a
 * retried delivery cannot dispatch twice.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { BUILD_TARGETS, isKnownTarget } from "../build-targets.ts";
import { dispatchFirmwareBuilds, findLatestRun } from "../api.ts";
import { checkAndStartCooldown, RegistryUnavailable, claimInteraction } from "../db.ts";
import { message, optionString } from "../discord.ts";
import { defer } from "../followup.ts";

const COOLDOWN_MS = 15 * 60 * 1000;
const DEFAULT_REF = "main";

export const definition: CommandDefinition = {
	name: "build",
	description: "Dispatch a firmware build",
	type: 1,
	options: [
		{
			name: "target",
			description: "Which firmware to build",
			type: 3,
			required: true,
			choices: BUILD_TARGETS.map((t) => ({ name: t.name, value: t.value })),
		},
	],
};

function formatRemaining(ms: number): string {
	const minutes = Math.ceil(ms / 60_000);
	return minutes <= 1 ? "less than a minute" : `about ${minutes} minutes`;
}

export function handler(c: CommandContext): Response {
	const target = optionString(c.interaction, "target", 32);
	if (!target || !isKnownTarget(target)) {
		// Discord validates choices, so reaching here means a stale client or a
		// crafted payload. Answered immediately, not deferred: nothing to dedupe
		// or rate-limit for a request that was never going to dispatch.
		return message("That is not a target this workflow knows about. Nothing was dispatched.");
	}

	return defer(c, async () => {
		try {
			const mine = await claimInteraction(c.env.DB, c.interaction.id);
			if (!mine) {
				return "Already dispatched. This was a duplicate delivery from Discord, not a second request.";
			}
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] dedupe unavailable for /build:`, err.cause ?? err);
				return (
					"The registry is unreachable, so I cannot safely dedupe this request and " +
					"will not risk a double dispatch. Try again shortly."
				);
			}
			throw err;
		}

		const userId = c.interaction.member?.user?.id ?? c.interaction.user?.id;
		if (!userId) return "Could not tell who invoked that. Nothing was dispatched.";

		try {
			const cooldown = await checkAndStartCooldown(c.env.DB, userId, COOLDOWN_MS, Date.now());
			if (!cooldown.ready) {
				return `/build is on cooldown for you. Try again in ${formatRemaining(cooldown.remainingMs)}.`;
			}
		} catch (err) {
			if (err instanceof RegistryUnavailable) {
				console.error(`[${c.correlationId}] cooldown check unavailable:`, err.cause ?? err);
				return (
					"The registry is unreachable, so I cannot enforce the cooldown and will not " +
					"risk letting this through uncapped. Try again shortly."
				);
			}
			throw err;
		}

		const since = Date.now();
		const dispatched = await dispatchFirmwareBuilds(c.env, DEFAULT_REF, target, c.correlationId);
		if (!dispatched.ok) {
			return (
				`Could not dispatch the build (status ${dispatched.status}). Nothing is running. ` +
				`Quote \`${c.correlationId}\` when reporting it.`
			);
		}

		const runUrl = await findLatestRun(c.env, since, c.correlationId);
		return `Dispatched \`${target}\`: ${runUrl}`;
	});
}
