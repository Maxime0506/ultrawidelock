/**
 * @file `/size` — the current recorded CDK size baseline.
 *
 * Reads `firmware/size-baseline.json` directly (`src/size-baseline.ts`), the
 * same file `make cdk-size-check` compares a build against and
 * `make cdk-size-baseline` rewrites. This is a snapshot from the last commit
 * that updated it, not a live measurement: nothing here can build firmware,
 * so a stale answer is possible if the record has not been refreshed since a
 * change moved the numbers. The commit the baseline was recorded at is always
 * printed so a reader can judge that for themselves.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { message } from "../discord.ts";
import { primaryBaseline, type RegionUsage } from "../size-baseline.ts";

export const definition: CommandDefinition = {
	name: "size",
	description: "The recorded CDK flash/RAM baseline for the shipping image",
	type: 1,
};

function fmt(region: RegionUsage): string {
	return `${region.used.toLocaleString()} / ${region.size.toLocaleString()} B (${region.pct}%, ${region.free.toLocaleString()} B free)`;
}

export function handler(_c: CommandContext): Response {
	const b = primaryBaseline();

	if (!b) {
		return message(
			"`firmware/size-baseline.json` did not parse the way this command expects, so I " +
				"have no number to report rather than a wrong one. `mk/cdk.mk:698-703` is where " +
				"it is regenerated (`make cdk-size-baseline`).",
			{ ephemeral: false },
		);
	}

	return message(
		`**${b.board}**, config \`${b.config}\` (the shipping image: SMP + RELEASE + LTO):\n` +
			`FLASH: ${fmt(b.regions.FLASH)}\n` +
			`RAM: ${fmt(b.regions.RAM)}\n` +
			`Recorded at commit \`${b.commit.slice(0, 12)}\`. Not live: this is the last committed ` +
			`baseline, refreshed by \`make cdk-size-baseline\` (\`mk/cdk.mk:698-703\`) and checked ` +
			`against a build by \`make cdk-size-check\`.`,
		{ ephemeral: false },
	);
}
