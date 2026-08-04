/**
 * @file The images somebody can be running.
 *
 * The make targets and their build directories, from the dispatcher in
 * `Makefile` and the per-target recipes in `mk/`. Bare targets mean the
 * DWM3001CDK; the nRF5340 DK is `nrf-` prefixed and the ESP32 is `esp-`
 * prefixed.
 *
 * "Not sure" is a real option on purpose. Somebody who does not know which
 * image they flashed is exactly the person filing the report, and forcing a
 * guess would put a wrong answer into the context block rather than a blank.
 */
export const IMAGES = [
	{ name: "make build — reader + Matter (build/cdk-matter)", value: "build" },
	{ name: "make reader — no Matter, no Thread (build/cdk-reader)", value: "reader" },
	{ name: "make selftest — UWB self-test (build/cdk-selftest)", value: "selftest" },
	{ name: "make nrf-build — nRF5340 DK", value: "nrf-build" },
	{ name: "make esp-build — ESP32", value: "esp-build" },
	{ name: "Not sure", value: "unknown" },
] as const;

const VALUES: readonly string[] = IMAGES.map((i) => i.value);

export function isKnownImage(value: string): boolean {
	return VALUES.includes(value);
}

export function imageLabel(value: string): string {
	return IMAGES.find((i) => i.value === value)?.name ?? value;
}
