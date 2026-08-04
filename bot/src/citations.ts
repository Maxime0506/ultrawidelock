/**
 * @file The triage table. Every answer this bot gives comes from here.
 *
 * A lookup table, not a model. Each entry is a plain reading, a next command,
 * and a `file:line` into this repository. Nothing is written from memory: if a
 * failure mode is not documented in the tree it does not get an entry, and the
 * bot escalates instead of guessing. A confident wrong diagnosis costs somebody
 * an evening, which is worse than no answer at all.
 *
 * `expect` is the drift guard. scripts/check-citations.ts reads each cited line
 * and fails if the substring is no longer on it, so an edit to mk/cdk.mk that
 * moves a line breaks CI rather than quietly turning this table into folklore.
 * Keep `expect` short and distinctive, and never let it span a line break.
 */

import { signatureCitations } from "./signatures.ts";

export interface Citation {
	/** Repository-relative path. */
	file: string;
	/** 1-indexed, matching what an editor and a `file:line` link both show. */
	line: number;
	/** Must still appear on that line. */
	expect: string;
}

export interface Topic {
	/** The Discord choice value, and the id used in a thread's context block. */
	id: string;
	/** The Discord choice label. */
	label: string;
	/** The symptom as somebody would report it. */
	symptom: string;
	/** What is actually happening. */
	reading: string;
	/** What to do next. */
	next: string;
	citations: Citation[];
}

export const TOPICS: Topic[] = [
	{
		id: "no-serial-port",
		label: "No serial port on the DWM3001CDK",
		symptom: "There is no /dev/ttyACM0 (or COM port) when I plug the CDK in.",
		reading:
			"Expected. This board has no UART console at all: the console is on RTT. " +
			"It is RTT rather than UART because the delayed-TX reply window on this " +
			"single-core part cannot absorb UART latency.",
		next: "`make monitor`, which reads RTT over probe-rs. Attach with the ELF you flashed, not one you merely built.",
		citations: [
			{ file: "firmware/prj.conf", line: 226, expect: "CONFIG_UART_CONSOLE=n" },
			{ file: "firmware/prj.conf", line: 250, expect: "RTT, not UART" },
		],
	},
	{
		id: "no-shell",
		label: "No shell on the Matter image",
		symptom: "The console comes up but there is no shell prompt and no commands.",
		reading:
			"Intentional. The Matter over Thread image turns the shell and USB CDC ACM " +
			"off, because that image is already close to full.",
		next: "`make reader` builds the same source without Matter or Thread, and that image has the shell.",
		citations: [
			{ file: "firmware/overlay-thread.conf", line: 145, expect: "CONFIG_SHELL=n" },
			{ file: "firmware/overlay-thread.conf", line: 147, expect: "CONFIG_USB_CDC_ACM=n" },
		],
	},
	{
		id: "hang-on-release",
		label: "Board hangs with no log (RELEASE=1)",
		symptom: "Built with RELEASE=1 and the board just stops. Nothing in the log.",
		reading:
			"On a release image a fault reads as a hang. The 1 KB ring truncates the " +
			"boot log, and NO_BLOCK_SKIP drops the NEWEST lines, so the fault message " +
			"is the part that goes missing. Debug is the default for exactly this reason.",
		next: "Rebuild without `RELEASE=1` and reproduce. Add `LTO=0` if you need a stack trace to name every frame.",
		citations: [
			{ file: "mk/cdk.mk", line: 286, expect: "NO_BLOCK_SKIP drops the NEWEST lines" },
			{ file: "mk/cdk.mk", line: 287, expect: "LTO is ON by default" },
		],
	},
	{
		id: "two-probes",
		label: "flash or monitor refuses to run",
		symptom: "make flash exits saying more than one debug probe is attached.",
		reading:
			"Working as intended. With two probes attached and CDK_PROBE unset it " +
			"refuses rather than guessing, because the enumeration order that decides " +
			"which one is 'probe 0' moves between sessions, and a wrong flash writes " +
			"another board's part.",
		next: "`make flash CDK_PROBE=<VID:PID:Serial>`, or `export PROBE_RS_PROBE=<VID:PID:Serial>` once per shell. `probe-rs list` prints the serials.",
		citations: [
			{ file: "mk/cdk.mk", line: 49, expect: "more than one debug probe is attached" },
			{ file: "mk/cdk.mk", line: 330, expect: "moves between sessions" },
		],
	},
	{
		id: "nfc-no-distance",
		label: "NFC tap reports no distance",
		symptom: "Tapping the reader does something, but no ranging or distance is published.",
		reading: "Expected. An NFC tap alone runs no ranging, so nothing is published.",
		next: "Approach the reader so a ranging session runs, rather than tapping only.",
		citations: [
			{ file: "docs/home-assistant.md", line: 146, expect: "An NFC tap alone runs no ranging" },
		],
	},
	{
		id: "agent-holds-port",
		label: "Home Assistant agent cannot open the port",
		symptom: "The HA agent will not start, or the serial port is busy.",
		reading:
			"The agent holds the serial port exclusively for as long as it runs, so " +
			"anything else already attached to that port blocks it.",
		next: "Close the terminal monitor first, then start the agent.",
		citations: [
			{ file: "docs/home-assistant.md", line: 94, expect: "agent holds the serial port exclusively" },
		],
	},
	{
		id: "flash-erase-cost",
		label: "What flash-erase destroys",
		symptom: "Is it safe to run make flash-erase?",
		reading:
			"It costs everything the board learned at runtime: the Matter fabrics, the " +
			"reader identity and its trust anchors, so Apple Home has to commission it " +
			"again. It also destroys OpenThread's SRP client key.",
		next: "To clear only what a controller can see, hold SW2 through reset instead. That has the same effect on fabrics and anchors and keeps the Thread settings, so the board comes back on the name it had.",
		citations: [
			{ file: "mk/cdk.mk", line: 343, expect: "Costs everything the board learned at runtime" },
			{ file: "mk/cdk.mk", line: 350, expect: "hold SW2 through reset instead" },
		],
	},
];

/**
 * What `/decode-devid` knows. The tree documents exactly two outcomes for the
 * raw DEV_ID, and this encodes those two and nothing else. A value that is
 * neither is reported as unrecognised, never as a guess.
 */
export const DEVID = {
	/** The healthy read: the top 24 bits identify the part. */
	healthyPrefix: "deca03",
	/** The two documented failure reads. */
	deadValues: ["00000000", "ffffffff"],
	citations: [
		{ file: "mk/cdk.mk", line: 317, expect: "0xDECA03xx" },
		{ file: "mk/cdk.mk", line: 318, expect: "up as 0x00000000 or 0xFFFFFFFF" },
		{ file: "mk/cdk.mk", line: 321, expect: "Read it with: make monitor" },
	] as Citation[],
};

/** What `/size` cites for how the baseline it reads is regenerated. The
 *  figures themselves come straight from `firmware/size-baseline.json`
 *  (`src/size-baseline.ts`), which needs no drift check: importing the file
 *  directly means there is nothing to transcribe out of sync. */
export const SIZE_CITATION: Citation = {
	file: "mk/cdk.mk",
	line: 689,
	expect: "cdk-size-baseline: record the current tree as the baseline",
};

/**
 * What `/twin` cites, and the firmware constants it re-types.
 *
 * These began as string literals inside twin.ts and commands/twin.ts, which
 * put them outside this table and therefore outside `check-citations.ts` — the
 * one thing in this bot that notices a cited line moving. They were all still
 * accurate, which is exactly the problem: correct today, with nothing watching.
 *
 * The `expect` for a `#define` deliberately includes its **value**, not just
 * its name, so `FIRA_RANGE_TRUST_K` changing from 3 to 4 fails this gate rather
 * than leaving `/twin` quietly explaining the old threshold. That is the whole
 * reason these are worth citing: the numbers are re-typed into TypeScript in
 * twin.ts and cannot be imported from C.
 */
export const TWIN = {
	blockCadence: {
		file: "modules/woz_uwb/src/driver/uwb_selftest.c",
		line: 36,
		expect: "block_duration_ms = 192u",
	},
	negTol: {
		file: "modules/woz_uwb/src/fira/fira_session.h",
		line: 88,
		expect: "FIRA_RANGE_NEG_TOL_CM 30",
	},
	maxCm: {
		file: "modules/woz_uwb/src/fira/fira_session.h",
		line: 89,
		expect: "FIRA_RANGE_MAX_CM     3000",
	},
	trustK: {
		file: "modules/woz_uwb/src/fira/fira_session.h",
		line: 111,
		expect: "FIRA_RANGE_TRUST_K   3",
	},
	spreadCm: {
		file: "modules/woz_uwb/src/fira/fira_session.h",
		line: 112,
		expect: "FIRA_RANGE_SPREAD_CM 50",
	},
	layer1Plausible: {
		file: "modules/woz_uwb/src/fira/fira_session.c",
		line: 174,
		expect: "Layer 1: reject a physically impossible",
	},
	layer4Trust: {
		file: "modules/woz_uwb/src/fira/fira_session.c",
		line: 187,
		expect: "Layer 4: consecutive plausible blocks",
	},
	legPrePoll: {
		file: "web-twin/twin_glue.c",
		line: 143,
		expect: "Pre-POLL: stash",
	},
	legPoll: {
		file: "web-twin/twin_glue.c",
		line: 150,
		expect: "POLL result (cper=0)",
	},
	legResponse: {
		file: "web-twin/twin_glue.c",
		line: 155,
		expect: "Response TXFRS",
	},
	legFinal: {
		file: "web-twin/twin_glue.c",
		line: 159,
		expect: "Final RFRAME",
	},
	legFinalData: {
		file: "web-twin/twin_glue.c",
		line: 165,
		expect: "Final_Data: the initiator intervals",
	},
} as const satisfies Record<string, Citation>;

/** `modules/woz_uwb/src/fira/fira_session.h:111` — one citation as a bare
 *  `file:line`, for building a reply string. Ranges are display-only: cite the
 *  anchor line here, and write the span into the sentence around it. */
export function cite(c: Citation): string {
	return `${c.file}:${c.line}`;
}

/** Every citation the bot can print, for the drift checker. */
export function allCitations(): Citation[] {
	return [
		...TOPICS.flatMap((t) => t.citations),
		...DEVID.citations,
		...signatureCitations(),
		SIZE_CITATION,
		...Object.values(TWIN),
	];
}

/** `` `mk/cdk.mk:293` `` — the form a terminal turns into a link. */
export function formatCitations(citations: Citation[]): string {
	return citations.map((c) => `\`${c.file}:${c.line}\``).join(", ");
}
