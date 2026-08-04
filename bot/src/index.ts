/**
 * @file The interactions endpoint.
 *
 * An HTTP interactions Worker, not a gateway bot: no persistent socket, no
 * privileged intents, no process with an uptime obligation. Discord POSTs
 * here, this file answers within the 3 second deadline or defers.
 *
 * The order below is load-bearing and is checked by index.test.ts:
 *
 *   1. reject anything that is not a POST
 *   2. read the body as TEXT, never as JSON
 *   3. verify Ed25519 over timestamp + that exact text
 *   4. only then parse
 *
 * Verifying after parsing would still reject bad signatures, but it would
 * run a JSON parser on unauthenticated input first, and it would answer
 * Discord's deliberately invalid PING with a 400 instead of a 401. Discord
 * refuses the URL for the second one.
 */
import { Hono } from "hono";
import type { Env } from "./env.ts";
import {
	type Interaction,
	InteractionResponseType,
	InteractionType,
	jsonResponse,
	message,
	newCorrelationId,
} from "./discord.ts";
import { componentHandlerFor, handlers, modalHandlerFor } from "./commands/index.ts";
import { verifySignature } from "./verify.ts";
import { escalateMinutesFrom, runAbandonedLinkPurge, runEscalationSweep } from "./scheduled.ts";
import { handleDiscordCallback, handleGithubCallback, startLinkedRole } from "./linkedRoles.ts";

/** One body of the size Discord will ever send, with room to spare. Read
 *  before verification, so it is bounded before it is trusted. */
const MAX_BODY_BYTES = 256 * 1024;

const app = new Hono<{ Bindings: Env }>();

app.get("/", (c) =>
	c.text("openaliro compatibility bot. This endpoint takes signed Discord interactions over POST.\n"),
);

// Linked Roles: ordinary browser redirects, not Discord interactions — no
// Ed25519 signature is sent or expected on any of these three, which is
// exactly why they are not folded into the POST "/" handler above.
app.get("/linked-role", (c) => startLinkedRole(c.env, c.req.url, newCorrelationId()));
app.get("/discord-oauth-callback", (c) => handleDiscordCallback(c.env, c.req.url, newCorrelationId()));
app.get("/github-oauth-callback", (c) => handleGithubCallback(c.env, c.req.url, newCorrelationId()));

app.post("/", async (c) => {
	const correlationId = newCorrelationId();

	const declared = Number(c.req.header("content-length") ?? "0");
	if (declared > MAX_BODY_BYTES) {
		return c.text("payload too large", 413);
	}

	const rawBody = await c.req.text();
	if (rawBody.length > MAX_BODY_BYTES) {
		return c.text("payload too large", 413);
	}

	// Fails closed on a missing binding: an endpoint that cannot verify has
	// not verified. The real reason goes to the log, never to the caller.
	if (!c.env.DISCORD_PUBLIC_KEY) {
		console.error(`[${correlationId}] DISCORD_PUBLIC_KEY is not bound; rejecting`);
	}

	const verified = await verifySignature(
		rawBody,
		c.req.header("x-signature-ed25519") ?? null,
		c.req.header("x-signature-timestamp") ?? null,
		c.env.DISCORD_PUBLIC_KEY ?? "",
	);
	if (!verified) {
		return c.text("invalid request signature", 401);
	}

	let interaction: Interaction;
	try {
		interaction = JSON.parse(rawBody) as Interaction;
	} catch {
		console.error(`[${correlationId}] signed body was not JSON`);
		return c.text("malformed interaction payload", 400);
	}

	if (interaction.type === InteractionType.Ping) {
		return jsonResponse({ type: InteractionResponseType.Pong });
	}

	if (interaction.type === InteractionType.ApplicationCommand) {
		const handler = interaction.data?.name ? handlers.get(interaction.data.name) : undefined;
		if (!handler) {
			console.error(`[${correlationId}] no handler for command ${interaction.data?.name}`);
			return message(
				`That command is registered with Discord but this Worker has no handler for it. ` +
					`Quote \`${correlationId}\` when reporting it.`,
			);
		}

		try {
			return await handler({ interaction, env: c.env, ctx: c.executionCtx, correlationId });
		} catch (err) {
			console.error(`[${correlationId}] handler threw:`, err);
			return message(
				`That failed inside the bot and nothing was changed. Retrying is safe. ` +
					`Quote \`${correlationId}\` when reporting it.`,
			);
		}
	}

	if (interaction.type === InteractionType.MessageComponent) {
		const customId = interaction.data?.custom_id ?? "";
		const handler = componentHandlerFor(customId);
		if (!handler) {
			console.error(`[${correlationId}] no handler for component ${customId}`);
			return message(
				`That button is not one this Worker handles. Quote \`${correlationId}\` when reporting it.`,
				{ ephemeral: true },
			);
		}

		try {
			return await handler({ interaction, env: c.env, ctx: c.executionCtx, correlationId });
		} catch (err) {
			console.error(`[${correlationId}] component handler threw:`, err);
			return message(
				`That failed inside the bot and nothing was changed. Retrying is safe. Quote \`${correlationId}\` when reporting it.`,
				{ ephemeral: true },
			);
		}
	}

	if (interaction.type === InteractionType.ModalSubmit) {
		const customId = interaction.data?.custom_id ?? "";
		const handler = modalHandlerFor(customId);
		if (!handler) {
			console.error(`[${correlationId}] no handler for modal ${customId}`);
			return message(
				`That form is not one this Worker handles. Quote \`${correlationId}\` when reporting it.`,
			);
		}

		try {
			return await handler({ interaction, env: c.env, ctx: c.executionCtx, correlationId });
		} catch (err) {
			console.error(`[${correlationId}] modal handler threw:`, err);
			return message(
				`That failed inside the bot and nothing was posted. Retrying is safe. ` +
					`Quote \`${correlationId}\` when reporting it.`,
			);
		}
	}

	console.error(`[${correlationId}] unsupported interaction type ${interaction.type}`);
	return c.text("unsupported interaction type", 400);
});

/** Cloudflare's module Worker format wants one default export implementing
 *  every handler this Worker has (`fetch` and, since the escalation sweep
 *  needs a Cron Trigger, `scheduled`) — Hono's own `app` object only ever
 *  implements `fetch`. Wrapped in an arrow rather than exporting `app.fetch`
 *  directly, so this still calls it as `app.fetch(...)` (a method call on
 *  `app`) rather than a detached function reference, matching exactly how
 *  the test suite already invokes it. */
export default {
	fetch: (request: Request, env: Env, ctx: ExecutionContext) => app.fetch(request, env, ctx),
	async scheduled(_controller: unknown, env: Env, ctx: ExecutionContext): Promise<void> {
		const correlationId = newCorrelationId();
		const now = Date.now();
		ctx.waitUntil(
			runEscalationSweep(env.DB, env.DISCORD_BOT_TOKEN, correlationId, now, escalateMinutesFrom(env)),
		);
		// Independently, so a missing bot token cannot leave live OAuth tokens
		// sitting in D1 forever.
		ctx.waitUntil(runAbandonedLinkPurge(env.DB, correlationId, now));
	},
};
