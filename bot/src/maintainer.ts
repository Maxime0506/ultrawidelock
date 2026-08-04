/**
 * @file The maintainer allow-list, shared by every maintainer-only command
 * (`/who-has`, `/test-request`). A `default_member_permissions: "0"` on the
 * command definition hides it from non-administrators, but that is a guild
 * setting anyone with server admin can change; this check is not.
 */

/** Exact match against the configured list. Empty list means nobody, which
 *  fails closed: an unset binding must not open the command up. */
export function isMaintainer(env: { MAINTAINER_IDS?: string }, userId: string): boolean {
	return (env.MAINTAINER_IDS ?? "")
		.split(/[\s,]+/)
		.filter(Boolean)
		.includes(userId);
}
