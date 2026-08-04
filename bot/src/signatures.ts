/**
 * @file The console-output matcher.
 *
 * A lookup table, not a model. Every entry below is a literal string that this
 * repository already documents, with the line that documents it. Nothing here
 * was written from what a log "probably" means: if the tree does not say it,
 * there is no entry, and the bot escalates instead.
 *
 * Patterns are deliberately literal and distinctive. A loose pattern that
 * matches half the pastes in a channel is worse than no pattern at all,
 * because it produces a confident answer that happens to be wrong, and the
 * person believes it for an evening before going back to the start.
 *
 * Ranking is by the length of the text that matched, longest first, so a
 * specific error string outranks a short token that happened to appear. Ties
 * keep declaration order. All matches are shown, never just the best one:
 * `URSK_Unavailable` legitimately has two documented causes and choosing
 * between them is the reader's job, not this table's.
 */
import type { Citation } from "./citations.ts";

export interface Signature {
	id: string;
	/** Must be distinctive enough that a match means something. Never `/g`:
	 *  a global regex carries lastIndex between calls, so the same paste would
	 *  match on one invocation and not the next. */
	pattern: RegExp;
	/**
	 * A second condition the whole text must also satisfy.
	 *
	 * For the entries whose giveaway string is not unique on its own. "No
	 * Response" is what Apple Home shows, but it is also a phrase somebody
	 * writes about a probe, and answering a probe question with four Matter
	 * bugs is exactly the confident wrong answer this table must not produce.
	 */
	context?: RegExp;
	/** What the tree says this is. */
	reading: string;
	/** What to do next. */
	next: string;
	citations: Citation[];
}

const T = "docs/troubleshooting.md";
const S = "docs/dwm3001cdk-surgery.md";

export const SIGNATURES: Signature[] = [
	{
		id: "devid-dead",
		pattern: /\bDEV_ID\b[^\n]*\b0x(?:0{8}|[fF]{8})\b|\b0x(?:0{8}|[fF]{8})\b[^\n]*\bDEV_ID\b/,
		reading:
			"The DW3110 is not answering over SPI. Both of these reads mean a wrong pin, " +
			"a wrong SPI mode, or an unpowered DW3110, and the tree does not narrow it " +
			"further than those three.",
		next: "`make selftest`, then `make monitor CDK_RTT_BUILD=build/cdk-selftest`, and read the raw DEV_ID directly.",
		citations: [
			{ file: "mk/cdk.mk", line: 317, expect: "0xDECA03xx" },
			{ file: "mk/cdk.mk", line: 318, expect: "up as 0x00000000 or 0xFFFFFFFF" },
		],
	},
	{
		id: "two-probes",
		pattern: /more than one debug probe is attached/i,
		reading:
			"Not a fault. `flash` and `monitor` refuse to guess which board to write " +
			"when two probes are attached, because the enumeration order that decides " +
			"'probe 0' moves between sessions.",
		next: "Re-run with `CDK_PROBE=<VID:PID:Serial>`, or `export PROBE_RS_PROBE=<VID:PID:Serial>` once per shell. `probe-rs list` prints the serials.",
		citations: [
			{ file: "mk/cdk.mk", line: 49, expect: "more than one debug probe is attached" },
			{ file: "mk/cdk.mk", line: 330, expect: "moves between sessions" },
		],
	},
	{
		id: "flash-worker-timeout",
		pattern: /Timed out waiting for response from worker/i,
		reading:
			"After 'Verifying image', this has usually programmed the board correctly " +
			"and only failed on the reset.",
		next: "Check with a read before reflashing, rather than reflashing on the assumption it failed.",
		citations: [{ file: T, line: 53, expect: "Timed out waiting for response from worker" }],
	},
	{
		id: "signing-key",
		pattern: /\bdfu-key\b|signing key|BOOT_SIGNATURE_KEY_FILE/i,
		reading:
			"Every image on these boards is signed and the key is gitignored, so a fresh " +
			"clone or a new git worktree has none. The build refuses rather than falling " +
			"back to the demo key published in MCUboot's own repository.",
		next: "`make dfu-key`, once per clone. `make nrf-build` stops the same way and takes the same fix.",
		citations: [{ file: T, line: 24, expect: "complaining about a signing key" }],
	},
	{
		id: "monitor-silent",
		pattern: /monitor[^\n]*(prints? nothing|no output|nothing (?:is )?printed)|attaches cleanly and prints nothing/i,
		reading:
			"Usually the ELF. probe-rs reads the RTT control block address out of the ELF " +
			"you pass it, so attaching with one you built but did not flash reads a stale " +
			"address, which looks exactly like a dead board.",
		next: "Reflash, or point `CDK_RTT_BUILD` at the image the board is actually running.",
		citations: [{ file: T, line: 31, expect: "attaches cleanly and prints nothing" }],
	},
	{
		id: "probe-held",
		pattern: /jlinkarm_nrf_worker|JLinkGUIServerExe|probe enumerates but/i,
		reading:
			"A stale process still holds the USB interface, usually after an interrupted " +
			"`west flash` or a SIGTERMed `probe-rs attach`.",
		next: "`pkill -f 'jlinkarm_nrf_worker_osx'` and `pkill -f 'JLinkGUIServerExe'`. No replug needed.",
		citations: [{ file: T, line: 43, expect: "probe enumerates but nothing can connect" }],
	},
	{
		id: "ursk-unavailable-m1",
		pattern: /GeneralError\s+URSK_Unavailable/i,
		reading:
			"At M1 this is the ranging session id. It is derived from the AUTH0 " +
			"transaction id, not chosen by the reader, so a hardcoded session id names a " +
			"session the phone has no key for. This is never a wrong-URSK-value problem: " +
			"M1 carries no URSK-derived material, so a value mismatch would surface later, " +
			"at M2-M4 STS.",
		next: "Derive the session id from the AUTH0 transaction id.",
		citations: [{ file: T, line: 196, expect: "GeneralError URSK_Unavailable" }],
	},
	{
		id: "ursk-unavailable-trigger",
		pattern: /URSK_Unavailable/i,
		reading:
			"If tap works and only approach fails, the fault is UWB-specific. Either no " +
			"common protocol version was negotiated, or the reader never emitted the " +
			"`0x98` 'URSK ready' trigger.",
		next: "Check the version negotiation and the 0x98 trigger. `protocol-research.md` §4 and the field guide in §10.",
		citations: [
			{ file: T, line: 128, expect: "0x98" },
			{ file: T, line: 129, expect: "URSK_Unavailable" },
		],
	},
	{
		id: "dwt-probe-failed",
		pattern: /dwt_probe failed/i,
		reading:
			"The DW3000 was never brought up at boot, so the first SPI touch happens " +
			"inside a NimBLE host callback, where the shallow stack and missing init make " +
			"probing fail.",
		next: "Bring the radio up once from a dedicated startup task. Both ports now do this.",
		citations: [{ file: T, line: 182, expect: "dwt_probe failed: -1" }],
	},
	{
		id: "protocol-0-unsupported",
		pattern: /protocol 0 unsupported/i,
		reading:
			"The channel split was missed. Ranging SDUs ride their own GCM channel keyed " +
			"from BleSK with fresh per-direction counters, not the credential-auth " +
			"channel, so seeing the raw envelope type as a protocol number means the two " +
			"were conflated.",
		next: "Key the ranging channel from BleSK separately from the credential-auth channel.",
		citations: [{ file: T, line: 201, expect: "protocol 0 unsupported" }],
	},
	{
		id: "disconnect-531",
		pattern: /\breason\s*[:=]?\s*531\b/i,
		reading:
			"The reader did not send Reader-Status-Access-Protocol-Completed after " +
			"EXCHANGE. It is mandatory, not optional, and the phone drops the link about " +
			"1.8 s later.",
		next: "Send Reader-Status-Access-Protocol-Completed on the BleSK channel after a successful EXCHANGE.",
		citations: [{ file: T, line: 192, expect: "reason 531" }],
	},
	{
		id: "no-response-tile",
		pattern: /Matter Accessory\s*\/?\s*No Response|No Response/i,
		context: /matter|home|accessory|tile|commission/i,
		reading:
			"On this board that was four independent bugs, not one: the priming report " +
			"must fit the IPv6 MTU, the node must be able to initiate an exchange, it must " +
			"report on change, and it must also report on a timer. Each looked like the " +
			"whole problem on its own.",
		next: "Work through all four in `dwm3001cdk-surgery.md` §3. Fixing one and re-testing is what makes this take weeks.",
		citations: [{ file: S, line: 128, expect: "Matter Accessory / No Response" }],
	},
	{
		id: "adv-enomem",
		pattern: /bt_le_adv_start|-ENOMEM|\berr(?:or)?\s*[:=-]?\s*-12\b/i,
		context: /bt_le_adv|advertis|disconnect/i,
		reading:
			"`bt_le_adv_start()` returns -12 (-ENOMEM) when called from inside the " +
			"`disconnected` callback, because Zephyr has not released the connection " +
			"object yet.",
		next: "Defer the advert restart to a work item and retry. Log the failure too: the advert logging only on success is what hid this.",
		citations: [{ file: S, line: 261, expect: "-ENOMEM" }],
	},
	{
		id: "stack-guard",
		pattern: /MPU FAULT|STACK (?:OVERFLOW|CHECK FAIL)|stack overflow/i,
		reading:
			"A hint, not a diagnosis. On this board adding one feature produced three MPU " +
			"stack-guard faults on three different threads, and none was diagnosable from " +
			"the symptom: two presented as something else entirely, including a fault " +
			"mid-unlock right after SENT_AUTH0 and a board that froze 4 s into boot.",
		next: "Measure rather than estimate. `CONFIG_INIT_STACKS=y` fills stacks with 0xAA; count the leading 0xAA run against the map file. `dwm3001cdk-surgery.md` §1.3 has the recipe.",
		citations: [
			{ file: S, line: 43, expect: "three MPU stack-guard" },
			{ file: S, line: 61, expect: "CONFIG_INIT_STACKS=y" },
		],
	},
	{
		id: "auto-relock",
		pattern: /AutoRelockTime|relocks? \d+ ?s later/i,
		reading: "A fixed auto-relock timer is fighting approach unlock.",
		next: "Set `AutoRelockTime = 0` and drive relock from proximity with hysteresis.",
		citations: [{ file: T, line: 219, expect: "AutoRelockTime = 0" }],
	},
	{
		id: "gate-could-not-run",
		pattern: /COULD NOT RUN/,
		reading:
			"Deliberate, not a warning. The gate's tool is not installed, and CI will run " +
			"it whatever this machine has, so 'could not check' reads as 'not verified'.",
		next: "`make tools-install` fills the gap. To accept it for one run, scope it out by name: `SKIP=\"cbmc docs\" make verify`.",
		citations: [{ file: T, line: 224, expect: "COULD NOT RUN" }],
	},
];

export interface Match {
	signature: Signature;
	/** The exact text that matched, for ranking and for showing the reader why. */
	matched: string;
}

/**
 * Every signature that matches, most specific first.
 *
 * "Most specific" is the length of the matched text. It is a crude measure and
 * deliberately so: anything cleverer would be a scoring model, and a scoring
 * model is the thing this file exists not to be.
 */
export function matchSignatures(text: string): Match[] {
	const matches: Match[] = [];

	for (const signature of SIGNATURES) {
		if (signature.context && !signature.context.test(text)) continue;
		const found = signature.pattern.exec(text);
		if (found) matches.push({ signature, matched: found[0] });
	}

	return matches.sort((a, b) => b.matched.length - a.matched.length);
}

/** Citations from every signature, for the drift gate. */
export function signatureCitations(): Citation[] {
	return SIGNATURES.flatMap((s) => s.citations);
}
