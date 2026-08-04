/**
 * @file `/matrix` — the compatibility matrix.
 *
 * Public and non-ephemeral: this is the artifact people screenshot, and a
 * screenshot of a message only its poster can see is not useful to anyone
 * else. Tries the PNG render first; falls back to the monospace table from
 * bot/README's "phase 3" both when the per-user cooldown is still active
 * (PNG rendering is the expensive path) and when rendering itself throws,
 * so a WASM or layout failure degrades the command instead of erroring it.
 */
import type { CommandContext } from "../command.ts";
import type { CommandDefinition } from "../discord.ts";
import { invokerId } from "../discord.ts";
import { matrixTable, type ValidationStatus } from "../matrix.ts";
import { RegistryUnavailable, matrixCounts } from "../rigs.ts";
import { checkMatrixCooldown } from "../cooldown.ts";
import { renderMatrixPng } from "../render.ts";
import { deferRich, type DeferredResult } from "../followup.ts";
import { latestValidations, ValidationsUnavailable } from "../validations.ts";

export const definition: CommandDefinition = {
	name: "matrix",
	description: "Show which boards are covered on which iOS versions",
	type: 1,
};

/** One PNG render per user per window; a cooldown hit is not an error, it
 *  is a reason to use the cheap path instead. */
const COOLDOWN_MS = 30_000;

// Discord caps message content at 2000 characters. A truncation note has to
// fit inside that budget too, so the table itself is cut a little short of
// the real limit rather than exactly at it.
const CONTENT_BUDGET = 1900;

function textFallback(
	counts: Awaited<ReturnType<typeof matrixCounts>>,
	results: readonly ValidationStatus[],
	note?: string,
): string {
	const table = matrixTable(counts, results);
	if (!table) {
		return "Nobody has registered any hardware yet. Run `/ihave` to start filling this in.";
	}

	const legend =
		"⚠️ owned, not yet validated on that pair · ❓ nobody owns it\n" +
		"(✅ validated / ❌ known-broken appear once test results exist)";
	const generatedAt = `-# generated <t:${Math.floor(Date.now() / 1000)}:R>${note ? ` · ${note}` : ""}`;
	const body = `\`\`\`\n${table}\n\`\`\`\n${legend}\n${generatedAt}`;

	if (body.length <= CONTENT_BUDGET) return body;

	const fence = "```";
	const closeAt = body.lastIndexOf("\n", CONTENT_BUDGET - fence.length - 40);
	return `${body.slice(0, closeAt)}\n${fence}\n-# truncated to fit Discord's message limit\n${generatedAt}`;
}

export function handler(c: CommandContext): Response {
	return deferRich(
		c,
		async (ctx): Promise<DeferredResult> => {
			let counts: Awaited<ReturnType<typeof matrixCounts>>;
			try {
				counts = await matrixCounts(ctx.env.DB);
			} catch (err) {
				if (err instanceof RegistryUnavailable) {
					console.error(`[${ctx.correlationId}] matrix query failed:`, err.cause ?? err);
					return { content: `The registry is not reachable right now. Quote \`${ctx.correlationId}\` if it keeps failing.` };
				}
				throw err;
			}

			// A validations read failure degrades to "no results yet" (⚠️/❓
			// only) rather than failing the whole command: owner counts are
			// still correct and still worth showing.
			let results: ValidationStatus[] = [];
			try {
				results = await latestValidations(ctx.env.DB);
			} catch (err) {
				if (err instanceof ValidationsUnavailable) {
					console.error(`[${ctx.correlationId}] matrix validations query failed:`, err.cause ?? err);
				} else {
					throw err;
				}
			}

			const userId = invokerId(ctx.interaction);
			const cooldown = userId
				? await checkMatrixCooldown(ctx.env.DB, userId, COOLDOWN_MS, Date.now())
				: { ready: true as const };

			if (!cooldown.ready) {
				const seconds = Math.ceil(cooldown.remainingMs / 1000);
				return { content: textFallback(counts, results, `image render cools down for ${seconds}s`) };
			}

			try {
				const rendered = await renderMatrixPng({ counts, results, generatedAtLabel: `generated ${new Date().toISOString()}` });
				if (!rendered) {
					return { content: textFallback(counts, results) };
				}
				const note = rendered.truncated ? " · showing the most recent iOS versions only, run `/who-has` for the rest" : "";
				return {
					content: `-# generated <t:${Math.floor(Date.now() / 1000)}:R>${note}`,
					file: { bytes: rendered.png, filename: "matrix.png" },
				};
			} catch (err) {
				console.error(`[${ctx.correlationId}] matrix PNG render failed, falling back to text:`, err);
				return { content: textFallback(counts, results) };
			}
		},
		{ ephemeral: false },
	);
}
