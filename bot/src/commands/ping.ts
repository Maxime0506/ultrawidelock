/**
 * @file `/ping` — the liveness check.
 *
 * Answers only if the signature already verified, so a successful reply proves
 * three things at once: the endpoint is reachable, the public key binding is
 * the right one, and the Worker is inside the 3 second response deadline
 * without deferring. That is the whole point of it, and it is why this command
 * touches no binding: it must not be able to fail for a second reason.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { message } from "../discord.ts";

export const definition: CommandDefinition = {
	name: "ping",
	description: "Check that the triage endpoint is up and verifying signatures",
	type: 1,
};

export function handler(c: CommandContext): Response {
	return message(`pong. Signature verified, request \`${c.correlationId}\`.`);
}
