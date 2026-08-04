/**
 * @file The shape every command file exports: a Discord command definition
 * (for registration) and a handler (for dispatch), together so the two
 * never drift apart.
 *
 * Separate from commands/index.ts so that followup.ts can take a context
 * without importing the command table that imports it back.
 */
import type { Env } from "./env.ts";
import type { CommandDefinition, Interaction } from "./discord.ts";

/** The minimal execution-context surface commands can rely on. Deliberately
 *  narrower than Cloudflare's own `ExecutionContext` type, which Hono's
 *  `c.executionCtx` does not structurally match version-for-version. Any real
 *  `ExecutionContext` satisfies it, so handlers keep working either way. */
export interface ExecutionCtx {
	waitUntil(promise: Promise<unknown>): void;
	passThroughOnException(): void;
}

export interface CommandContext {
	interaction: Interaction;
	env: Env;
	/** Needed for waitUntil: a deferred command answers Discord immediately and
	 *  finishes its work after the response has gone. */
	ctx: ExecutionCtx;
	correlationId: string;
}

export type CommandHandler = (c: CommandContext) => Response | Promise<Response>;

/**
 * Modal submissions and button presses arrive as their own interactions, with
 * no command name on them — only the `custom_id` they were created with. A
 * module claims those by declaring a prefix here, rather than the router
 * keeping its own table: a hand-maintained table in commands/index.ts lets a
 * command ship a handler nobody routes to, and the two drift silently.
 */
export interface CommandModule {
	definition: CommandDefinition;
	handler: CommandHandler;
	modalPrefix?: string;
	onModalSubmit?: CommandHandler;
	componentPrefix?: string;
	onComponent?: CommandHandler;
}

export type { CommandDefinition };
