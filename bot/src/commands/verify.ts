/**
 * @file `/verify <sha256>` — attestation lookup by subject digest.
 *
 * Answers "did openaliro/openaliro's CI actually build this file", from
 * GitHub's public attestations API, not from a SHA256SUMS.txt served next to
 * the artifact it describes (a compromise that could replace the binary could
 * replace that file in the same motion — `scripts/security-attest.sh`'s own
 * reasoning for why this control exists at all).
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { lookupAttestation } from "../attest.ts";
import { message, optionString } from "../discord.ts";
import { defer } from "../followup.ts";

export const definition: CommandDefinition = {
	name: "verify",
	description: "Check whether a file's sha256 has a build-provenance attestation",
	type: 1,
	options: [
		{
			name: "sha256",
			description: "The digest, with or without a sha256: prefix",
			type: 3,
			required: true,
		},
	],
};

export function handler(c: CommandContext): Response {
	const raw = optionString(c.interaction, "sha256", 80);
	if (!raw) return message("Give me the sha256 digest of the file, 64 hex characters.");

	return defer(c, async () => {
		const outcome = await lookupAttestation(raw, c.env.GITHUB_READ_TOKEN, c.correlationId);

		if (!outcome.ok) {
			switch (outcome.reason) {
				case "invalid-digest":
					// Not echoed: it failed a hex-digest test, so whatever was typed
					// is not a sha256 either way.
					return "That is not a sha256 digest. It should be 64 hex characters, with or without a `sha256:` prefix.";
				case "not-found":
					return (
						"No attestation exists for that digest. Either it did not come from " +
						"this repository's release workflow, or the bytes do not match what " +
						"was published. Do not trust the file on the strength of a matching " +
						"filename alone."
					);
				case "rate-limited":
					return "GitHub is rate-limiting these lookups right now. Try again shortly.";
				case "error":
					return `Could not reach GitHub's attestation API (status ${outcome.status}). Quote \`${c.correlationId}\` if it keeps failing.`;
			}
		}

		const { result } = outcome;
		if (!result.found) {
			return "No attestation exists for that digest.";
		}

		return (
			`Found ${result.count} attestation${result.count === 1 ? "" : "s"} for that digest ` +
			`from **openaliro/openaliro**'s release workflow (predicate type: ` +
			`${result.predicateTypes.join(", ")}). That means CI built this, not that this ` +
			`particular file is safe to run; verify the digest yourself before trusting it.`
		);
	});
}
