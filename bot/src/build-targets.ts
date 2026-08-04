/**
 * @file The `targets` choices `firmware-builds.yml` actually accepts.
 *
 * Hand-copied from `.github/workflows/firmware-builds.yml`'s `workflow_dispatch`
 * input rather than parsed at request time, because a Worker has no access to
 * that file. `test/build-targets.test.ts` re-parses the live workflow and
 * asserts this list matches it exactly, so an added or renamed target fails
 * the build instead of `/build` silently offering a stale choice.
 */
export const BUILD_TARGETS = [
	{ name: "All six targets (~90 min)", value: "all" },
	{ name: "nrf — all three nRF jobs", value: "nrf" },
	{ name: "esp32 — all three ESP jobs", value: "esp32" },
	{ name: "dwm3001cdk", value: "dwm3001cdk" },
	{ name: "nrf5340dk", value: "nrf5340dk" },
	{ name: "nrf5340dk-aliro-blob", value: "nrf5340dk-aliro-blob" },
	{ name: "esp32-idf", value: "esp32-idf" },
	{ name: "esp32-initiator", value: "esp32-initiator" },
	{ name: "esp32-matter", value: "esp32-matter" },
] as const;

const VALUES: readonly string[] = BUILD_TARGETS.map((t) => t.value);

export function isKnownTarget(value: string): boolean {
	return VALUES.includes(value);
}
