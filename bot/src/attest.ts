/**
 * @file Look up a GitHub build-provenance attestation by subject digest.
 *
 * `release.yml` runs `actions/attest-build-provenance` on every published
 * asset (`.github/workflows/release.yml`, `scripts/security-attest.sh`), which
 * binds an artifact's sha256 to the workflow, repository and commit that built
 * it. This is the read half: "does this digest have one", answered from
 * GitHub's own public attestations API rather than trusted from a SHA256SUMS
 * file served next to the thing it describes.
 *
 * Read-only and needs no write scope. A token, if bound, only raises the
 * unauthenticated rate limit; the lookup works without one for a public repo.
 */

const REPO = "openaliro/openaliro";
const SHA256 = /^(?:sha256:)?([0-9a-f]{64})$/i;

export interface AttestationResult {
	found: boolean;
	count: number;
	predicateTypes: string[];
}

export type LookupOutcome =
	| { ok: true; result: AttestationResult }
	| { ok: false; reason: "invalid-digest" }
	| { ok: false; reason: "not-found" }
	| { ok: false; reason: "rate-limited" }
	| { ok: false; reason: "error"; status: number };

/** Accepts a bare 64-hex digest or one prefixed `sha256:`. Case-insensitive,
 *  normalised to lowercase, prefix added back for the API call. */
export function normaliseDigest(raw: string): string | null {
	const m = SHA256.exec(raw.trim());
	return m ? `sha256:${m[1]!.toLowerCase()}` : null;
}

interface AttestationsResponse {
	attestations?: { bundle?: { dsseEnvelope?: { payload?: string } } }[];
}

function predicateTypeOf(entry: { bundle?: { dsseEnvelope?: { payload?: string } } }): string {
	try {
		const payload = entry.bundle?.dsseEnvelope?.payload;
		if (!payload) return "unknown";
		const decoded = JSON.parse(atob(payload)) as { predicateType?: string };
		return decoded.predicateType ?? "unknown";
	} catch {
		return "unknown";
	}
}

export async function lookupAttestation(
	digest: string,
	token: string | undefined,
	correlationId: string,
): Promise<LookupOutcome> {
	const normalised = normaliseDigest(digest);
	if (!normalised) return { ok: false, reason: "invalid-digest" };

	const headers: Record<string, string> = {
		accept: "application/vnd.github+json",
		"user-agent": "openaliro-triage-bot",
	};
	if (token) headers.authorization = `Bearer ${token}`;

	let res: Response;
	try {
		res = await fetch(
			`https://api.github.com/repos/${REPO}/attestations/${normalised}`,
			{ headers },
		);
	} catch (err) {
		console.error(`[${correlationId}] attestation lookup did not complete:`, err);
		return { ok: false, reason: "error", status: 0 };
	}

	if (res.status === 404) return { ok: false, reason: "not-found" };
	if (res.status === 403 || res.status === 429) return { ok: false, reason: "rate-limited" };
	if (!res.ok) {
		console.error(`[${correlationId}] attestation lookup -> ${res.status} ${await res.text()}`);
		return { ok: false, reason: "error", status: res.status };
	}

	const body = (await res.json()) as AttestationsResponse;
	const entries = body.attestations ?? [];
	return {
		ok: true,
		result: {
			found: entries.length > 0,
			count: entries.length,
			predicateTypes: [...new Set(entries.map(predicateTypeOf))],
		},
	};
}
