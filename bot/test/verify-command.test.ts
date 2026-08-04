/**
 * @file `/verify` and the attestation lookup it wraps.
 *
 * fetch is stubbed at the module boundary (src/attest.ts calls the global
 * fetch), so these exercise the actual HTTP-shaped decision tree — 404,
 * rate limit, malformed digest — without a real call to GitHub.
 */
import { strict as assert } from "node:assert";
import { afterEach, describe, it } from "node:test";
import { lookupAttestation, normaliseDigest } from "../src/attest.ts";

const DIGEST = "a".repeat(64);

function stubFetch(respond: (url: string) => Response): () => void {
	const original = globalThis.fetch;
	globalThis.fetch = (async (input: RequestInfo | URL) => respond(String(input))) as typeof fetch;
	return () => void (globalThis.fetch = original);
}

describe("normaliseDigest", () => {
	it("accepts bare and prefixed 64-hex digests, case-insensitively", () => {
		assert.equal(normaliseDigest(DIGEST), `sha256:${DIGEST}`);
		assert.equal(normaliseDigest(`sha256:${DIGEST}`), `sha256:${DIGEST}`);
		assert.equal(normaliseDigest(DIGEST.toUpperCase()), `sha256:${DIGEST}`);
	});

	it("rejects the wrong length and non-hex input", () => {
		for (const bad of ["", "a".repeat(63), "a".repeat(65), "z".repeat(64), "sha1:" + DIGEST]) {
			assert.equal(normaliseDigest(bad), null, bad);
		}
	});
});

describe("lookupAttestation", () => {
	let restore: () => void;
	afterEach(() => restore?.());

	it("reports found with a predicate type", async () => {
		restore = stubFetch(
			() =>
				new Response(
					JSON.stringify({
						attestations: [
							{
								bundle: {
									dsseEnvelope: {
										payload: btoa(JSON.stringify({ predicateType: "https://slsa.dev/provenance/v1" })),
									},
								},
							},
						],
					}),
					{ status: 200 },
				),
		);
		const outcome = await lookupAttestation(DIGEST, undefined, "cid");
		assert.equal(outcome.ok, true);
		if (outcome.ok) {
			assert.equal(outcome.result.found, true);
			assert.equal(outcome.result.count, 1);
			assert.deepEqual(outcome.result.predicateTypes, ["https://slsa.dev/provenance/v1"]);
		}
	});

	it("reports not-found on a 404", async () => {
		restore = stubFetch(() => new Response("not found", { status: 404 }));
		const outcome = await lookupAttestation(DIGEST, undefined, "cid");
		assert.deepEqual(outcome, { ok: false, reason: "not-found" });
	});

	it("reports rate-limited on 403 and 429", async () => {
		for (const status of [403, 429]) {
			restore = stubFetch(() => new Response("", { status }));
			const outcome = await lookupAttestation(DIGEST, undefined, "cid");
			assert.deepEqual(outcome, { ok: false, reason: "rate-limited" });
			restore();
		}
	});

	it("rejects a malformed digest before making a network call", async () => {
		let called = false;
		restore = stubFetch(() => {
			called = true;
			return new Response("{}", { status: 200 });
		});
		const outcome = await lookupAttestation("not-a-digest", undefined, "cid");
		assert.deepEqual(outcome, { ok: false, reason: "invalid-digest" });
		assert.equal(called, false);
	});

	it("does not throw when the bundle payload cannot be decoded", async () => {
		restore = stubFetch(
			() =>
				new Response(JSON.stringify({ attestations: [{ bundle: {} }] }), { status: 200 }),
		);
		const outcome = await lookupAttestation(DIGEST, undefined, "cid");
		assert.equal(outcome.ok, true);
		if (outcome.ok) assert.deepEqual(outcome.result.predicateTypes, ["unknown"]);
	});

	it("sends the token when one is bound, and omits it otherwise", async () => {
		let sawAuth: string | null = null;
		const original = globalThis.fetch;
		globalThis.fetch = (async (_input: RequestInfo | URL, init?: RequestInit) => {
			sawAuth = (init?.headers as Record<string, string> | undefined)?.authorization ?? null;
			return new Response(JSON.stringify({ attestations: [] }), { status: 200 });
		}) as typeof fetch;
		restore = () => void (globalThis.fetch = original);

		await lookupAttestation(DIGEST, "a-token", "cid");
		assert.equal(sawAuth, "Bearer a-token");

		await lookupAttestation(DIGEST, undefined, "cid");
		assert.equal(sawAuth, null);
	});
});
