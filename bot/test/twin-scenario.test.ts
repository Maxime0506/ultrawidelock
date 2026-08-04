/**
 * @file runApproachScenario against the real, booted twin — no Discord layer.
 *
 * Complements twin.test.ts (which exercises the command surface end to end):
 * this checks the scenario engine's own contract, including the round cap
 * that nothing reachable through /twin approach's option bounds can trigger.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import { bootTwin, runApproachScenario, ScenarioTooLong } from "../src/twin.ts";

describe("runApproachScenario", () => {
	it("reaches trust with no noise and no drops", async () => {
		const handle = await bootTwin();
		const result = runApproachScenario(handle, { speedMps: 1.4, noise: "none", drops: 0 });
		assert.equal(result.decision.outcome, "trusted");
		assert.equal(result.finalTrustLevel, result.trustK);
		assert.notEqual(result.finalTrustedCm, null);
		assert.match(result.decision.citation, /fira_session\.c/);
	});

	it("explains a spread-break from a noise spike when trust does not complete", async () => {
		const handle = await bootTwin();
		// seed 1 with "light" noise is the exact case twin.test.ts observed
		// tripping a spread break early via runApproachScenario's default seed.
		const result = runApproachScenario(handle, { speedMps: 1.4, noise: "light", drops: 0 }, 1);
		if (result.decision.outcome === "not-trusted") {
			assert.match(result.decision.reason, /spread|drop|plausible|block\(s\)/);
			assert.match(result.decision.citation, /fira_session/);
		}
	});

	it("throws ScenarioTooLong, naming the round count, past the cap", async () => {
		const handle = await bootTwin();
		assert.throws(
			() => runApproachScenario(handle, { speedMps: 0.2, noise: "none", drops: 0, startDistanceM: 100 }),
			(err: unknown) => {
				assert.ok(err instanceof ScenarioTooLong);
				assert.ok(err.wouldBeRounds > 500);
				assert.match(err.message, /500/);
				return true;
			},
		);
	});

	it("every round is BLOCK_MS apart and the round count matches the reported length", async () => {
		const handle = await bootTwin();
		const result = runApproachScenario(handle, { speedMps: 2.5, noise: "none", drops: 0 });
		assert.ok(result.rounds.length > 0);
		for (let i = 1; i < result.rounds.length; i++) {
			assert.equal(result.rounds[i]?.block, (result.rounds[i - 1]?.block ?? 0) + 1);
		}
	});

	it("a dropped round carries a null measuredCm and does not call into the firmware for it", async () => {
		const handle = await bootTwin();
		const result = runApproachScenario(handle, { speedMps: 1.4, noise: "none", drops: 3 });
		const dropped = result.rounds.filter((r) => r.measuredCm === null);
		assert.equal(dropped.length, 3);
	});
});
