/**
 * @file The escalation sweep: "asleep candidates get pinged on a follow-up
 * if nobody accepts within a configurable window." Runs off a Cron Trigger
 * (wrangler.toml `[triggers]`) rather than any in-request timer, since a
 * Worker has no way to schedule work minutes after a request has already
 * finished — this is the one part of the bot that is not driven by a
 * Discord interaction at all.
 */
import { asleepUnpingedCandidates, markEscalated, pendingUnescalated } from "./testRequests.ts";
import { buildEscalationPing } from "./testRequestContainer.ts";
import { postMessage } from "./discordRest.ts";
import { purgeAbandonedLinks } from "./oauthLinks.ts";
import { STATE_TTL_MS } from "./oauthState.ts";

/** Spec: "a configurable window". This is the fallback when
 *  TEST_REQUEST_ESCALATE_MINUTES is unset or not a positive number — chosen
 *  so escalation still happens somewhat promptly rather than silently never
 *  firing, since never escalating is the wrong failure direction for a
 *  feature whose whole point is reachability. */
export const DEFAULT_ESCALATE_MINUTES = 30;

export function escalateMinutesFrom(env: { TEST_REQUEST_ESCALATE_MINUTES?: string }): number {
	const raw = Number(env.TEST_REQUEST_ESCALATE_MINUTES);
	return Number.isFinite(raw) && raw > 0 ? raw : DEFAULT_ESCALATE_MINUTES;
}

export interface SweepResult {
	requestsChecked: number;
	requestsEscalated: number;
	candidatesPinged: number;
}

/** One sweep tick. Every step degrades independently: a request whose
 *  candidate lookup or escalation-mark fails is skipped (logged) rather
 *  than aborting the whole sweep, so one bad row cannot block every other
 *  pending request's escalation. */
export async function runEscalationSweep(
	db: D1Database | undefined,
	botToken: string | undefined,
	correlationId: string,
	now: number,
	escalateMinutes: number,
): Promise<SweepResult> {
	const result: SweepResult = { requestsChecked: 0, requestsEscalated: 0, candidatesPinged: 0 };

	if (!botToken) {
		console.error(`[${correlationId}] escalation sweep skipped: no DISCORD_BOT_TOKEN bound`);
		return result;
	}

	let due: Awaited<ReturnType<typeof pendingUnescalated>>;
	try {
		due = await pendingUnescalated(db, now - escalateMinutes * 60_000);
	} catch (err) {
		console.error(`[${correlationId}] escalation sweep could not read pending requests:`, err);
		return result;
	}
	result.requestsChecked = due.length;

	for (const req of due) {
		let asleepIds: string[];
		try {
			asleepIds = await asleepUnpingedCandidates(db, req.id);
		} catch (err) {
			console.error(`[${correlationId}] escalation sweep could not read candidates for ${req.id}:`, err);
			continue;
		}

		if (asleepIds.length > 0) {
			await postMessage(botToken, correlationId, req.channel_id, buildEscalationPing(asleepIds));
			// A failed ping is logged inside postMessage; still mark the
			// request escalated below rather than retrying it forever.
		}

		try {
			await markEscalated(db, req.id, now, asleepIds);
		} catch (err) {
			console.error(`[${correlationId}] escalation sweep could not mark ${req.id} escalated:`, err);
			continue;
		}

		result.requestsEscalated += 1;
		result.candidatesPinged += asleepIds.length;
	}

	return result;
}

/**
 * Delete OAuth rows stranded between the two legs.
 *
 * Separate from the escalation sweep and not folded into it, because the two
 * have different dependencies: escalation needs a bot token and does nothing
 * without one, while this needs only D1. A deployment that never set
 * DISCORD_BOT_TOKEN must still not accumulate live credentials.
 *
 * The cutoff is the OAuth state TTL. Past it the state row is refused at read
 * time, so the flow that would have scrubbed these tokens can no longer be
 * completed by anyone — the row is unreachable, not merely slow.
 */
export async function runAbandonedLinkPurge(
	db: D1Database | undefined,
	correlationId: string,
	now: number,
): Promise<number> {
	try {
		const purged = await purgeAbandonedLinks(db, now - STATE_TTL_MS);
		if (purged > 0) console.log(`[${correlationId}] purged ${purged} abandoned OAuth row(s)`);
		return purged;
	} catch (err) {
		console.error(`[${correlationId}] abandoned-link purge failed:`, err);
		return 0;
	}
}
