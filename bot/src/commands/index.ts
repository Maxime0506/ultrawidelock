/**
 * @file The command table.
 *
 * Registration and dispatch read the same list, so a command cannot be
 * registered with Discord without a handler behind it, or gain a handler
 * nobody can invoke.
 *
 * The modal and component routes are derived from the modules too, rather than
 * kept as their own hand-written maps here: a table in this file lets a command
 * ship a handler that nothing routes to, and the two drift apart silently.
 */
import type { CommandHandler, CommandModule } from "../command.ts";

// --- triage ---
import * as build from "./build.ts";
import * as context from "./context.ts";
import * as decodeDevid from "./decode-devid.ts";
import * as helpMe from "./help-me.ts";
import * as size from "./size.ts";
import * as spec from "./spec.ts";
import * as twin from "./twin.ts";
import * as verify from "./verify.ts";
import * as why from "./why.ts";

// --- hardware compatibility ---
import * as forget from "./forget.ts";
import * as ihave from "./ihave.ts";
import * as matrix from "./matrix.ts";
import * as testRequest from "./test-request.ts";
import * as testResult from "./test-result.ts";
import * as whoHas from "./who-has.ts";

// --- shared ---
import * as ping from "./ping.ts";

const modules: CommandModule[] = [
	helpMe,
	ping,
	why,
	decodeDevid,
	ihave,
	whoHas,
	forget,
	context,
	spec,
	verify,
	size,
	build,
	twin,
	matrix,
	testRequest,
	testResult,
];

/** What `scripts/register-commands.ts` uploads. */
export const definitions = modules.map((m) => m.definition);

/** What the fetch handler dispatches on. */
export const handlers = new Map<string, CommandHandler>(
	modules.map((m) => [m.definition.name, m.handler]),
);

/** Longest prefix first, so a future `help-me-extra` cannot be swallowed by
 *  `help-me`. */
function routes(
	pick: (m: CommandModule) => { prefix?: string; handler?: CommandHandler },
): { prefix: string; handler: CommandHandler }[] {
	return modules
		.map(pick)
		.filter((r): r is { prefix: string; handler: CommandHandler } =>
			Boolean(r.prefix && r.handler),
		)
		.sort((a, b) => b.prefix.length - a.prefix.length);
}

/**
 * Modal submissions and button presses carry no command name, only the
 * custom_id they were opened with.
 */
const modalRoutes = routes((m) => ({ prefix: m.modalPrefix, handler: m.onModalSubmit }));
const componentRoutes = routes((m) => ({ prefix: m.componentPrefix, handler: m.onComponent }));

export function modalHandlerFor(customId: string): CommandHandler | undefined {
	return modalRoutes.find((r) => customId.startsWith(r.prefix))?.handler;
}

export function componentHandlerFor(customId: string): CommandHandler | undefined {
	return componentRoutes.find((r) => customId.startsWith(r.prefix))?.handler;
}
