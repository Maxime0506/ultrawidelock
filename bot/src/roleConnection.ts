/**
 * @file Linked Roles metadata: the five fields the spec suggests
 * (`boards_owned`, `has_nfc`, `ios_major`, `validated_runs`, `merged_prs`),
 * their Discord registration schema, and computing four of the five purely
 * from this bot's own D1 tables. `merged_prs` is deliberately not computed
 * here — it needs the linked GitHub account (githubOAuth.ts) and a call to
 * GitHub's own API, a different failure domain from "read our own D1".
 */
import { entriesForUser } from "./rigs.ts";
import { countValidationsByTester } from "./validations.ts";

/** Discord's ApplicationRoleConnectionMetadataType enum (verified against
 *  docs.discord.com 2026-08-04): only the two values this bot's five fields
 *  actually use. */
const METADATA_TYPE = {
	INTEGER_GREATER_THAN_OR_EQUAL: 2,
	BOOLEAN_EQUAL: 7,
} as const;

/** The registration payload for `PUT /applications/{id}/role-connections/metadata`
 *  (scripts/register-role-metadata.ts) — five is Discord's own maximum, and
 *  these are exactly the spec's suggested fields, gating each on "at least"
 *  a guild-configured threshold rather than an exact match, except the one
 *  genuinely boolean field. */
export const METADATA_RECORDS = [
	{
		key: "boards_owned",
		type: METADATA_TYPE.INTEGER_GREATER_THAN_OR_EQUAL,
		name: "Boards owned",
		description: "Number of distinct boards registered in the compatibility bot",
	},
	{
		key: "has_nfc",
		type: METADATA_TYPE.BOOLEAN_EQUAL,
		name: "Has NFC hardware",
		description: "Owns at least one board with an NFC front-end",
	},
	{
		key: "ios_major",
		type: METADATA_TYPE.INTEGER_GREATER_THAN_OR_EQUAL,
		name: "iOS major version",
		description: "Highest iOS major version registered against any owned board",
	},
	{
		key: "validated_runs",
		type: METADATA_TYPE.INTEGER_GREATER_THAN_OR_EQUAL,
		name: "Validated test runs",
		description: "Number of /test-result submissions recorded",
	},
	{
		key: "merged_prs",
		type: METADATA_TYPE.INTEGER_GREATER_THAN_OR_EQUAL,
		name: "Merged pull requests",
		description: "Merged pull requests authored by the linked GitHub account",
	},
] as const;

export interface RegistryMetadata {
	boards_owned: number;
	has_nfc: boolean;
	ios_major: number;
	validated_runs: number;
}

/** "19.1.2" -> 19; anything unparseable -> 0, same as "no board on that
 *  axis" rather than a thrown error, since a metadata field cannot fail a
 *  role-connection push over one malformed row. */
function majorVersion(iosVersion: string | null): number {
	if (!iosVersion) return 0;
	const n = Number.parseInt(iosVersion.split(".")[0] ?? "", 10);
	return Number.isFinite(n) ? n : 0;
}

/** The four fields this bot can compute without leaving D1. */
export async function computeRegistryMetadata(db: D1Database | undefined, discordUserId: string): Promise<RegistryMetadata> {
	const entries = await entriesForUser(db, discordUserId);
	const validatedRuns = await countValidationsByTester(db, discordUserId);
	return {
		boards_owned: entries.length,
		has_nfc: entries.some((e) => e.nfc !== "none"),
		ios_major: Math.max(0, ...entries.map((e) => majorVersion(e.ios_version))),
		validated_runs: validatedRuns,
	};
}

/**
 * Discord's role-connection push wants every metadata value "stringified"
 * (verified against docs.discord.com 2026-08-04 — the field itself is
 * documented as taking each value's string-ified form). The boolean
 * convention specifically ("1"/"0" vs "true"/"false") is not spelled out in
 * the same page; "1"/"0" is used here because the type's own doc describes
 * the *comparison* as integer-valued ("the metadata value (integer) is
 * equal to... (integer; 1)"), which is the strongest signal available
 * without a live round trip to confirm against.
 */
export function stringifyMetadata(m: RegistryMetadata & { merged_prs: number }): Record<string, string> {
	return {
		boards_owned: String(m.boards_owned),
		has_nfc: m.has_nfc ? "1" : "0",
		ios_major: String(m.ios_major),
		validated_runs: String(m.validated_runs),
		merged_prs: String(m.merged_prs),
	};
}
