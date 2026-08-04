/**
 * @file `/twin approach` and `/twin explain` — the WASM digital twin, run inside this Worker.
 *
 * `/twin approach` drives the real compiled woz_uwb responder (twin.ts,
 * web-twin/twin.js + twin.wasm) through a simulated walk-up and reports what
 * the trust gate actually decided, with a file:line citation. It answers
 * protocol/crypto-maths questions; it proves nothing about PDoA/AoA, NFC
 * Express Mode, iOS point-release behaviour, or physical approach unlock —
 * every reply says so.
 *
 * `/twin explain <hex>` is NOT implemented. twin_glue.c exports no entry
 * point that ingests a raw wire frame — every call in it synthesizes frames
 * from a target distance (twin_mk_prepoll/twin_mk_final_data), and the whole
 * exchange is CCM*-encrypted against a twin-internal test URSK a real board's
 * traffic was never encrypted under. Decoding a pasted ranging block from a
 * bug report would need a new C-side entry point in ccc_shim_rx.c/twin_glue.c
 * — a firmware change, which this instrumentation task is not allowed to
 * make (see the parent prompt's Hard Constraint 4). The subcommand is
 * registered so `/twin explain` names its own gap instead of 404ing, and
 * says exactly that rather than attempting a decode that cannot work.
 */
import type { CommandContext, CommandDefinition } from "../command.ts";
import { defer } from "../followup.ts";
import { InteractionResponseType, jsonResponse } from "../discord.ts";
import { TWIN } from "../citations.ts";
import {
	BLOCK_MS,
	bootTwin,
	FIRA_RANGE_SPREAD_CM,
	MAX_ROUNDS,
	runApproachScenario,
	ScenarioTooLong,
	type ApproachResult,
	type NoiseLevel,
} from "../twin.ts";

const MIN_SPEED = 0.2;
const MAX_SPEED = 2.5;
const NOISE_LEVELS: NoiseLevel[] = ["none", "light", "heavy"];
const MAX_DROPS = 50;

export const definition: CommandDefinition = {
	name: "twin",
	description: "Run the real firmware's WASM digital twin",
	type: 1,
	options: [
		{
			name: "approach",
			description: "Simulate a walk-up and report the trust-gate decision",
			type: 1, // SUB_COMMAND
			options: [
				{
					name: "speed",
					description: `Walk speed in m/s (${MIN_SPEED}-${MAX_SPEED})`,
					type: 10, // NUMBER
					required: false,
					min_value: MIN_SPEED,
					max_value: MAX_SPEED,
				},
				{
					name: "noise",
					description: "Range noise level",
					type: 3, // STRING
					required: false,
					choices: NOISE_LEVELS.map((n) => ({ name: n, value: n })),
				},
				{
					name: "drops",
					description: `Ranging blocks that never arrive (0-${MAX_DROPS})`,
					type: 4, // INTEGER
					required: false,
					min_value: 0,
					max_value: MAX_DROPS,
				},
			],
		},
		{
			name: "explain",
			description: "Not implemented — decoding a raw ranging block needs a firmware change",
			type: 1, // SUB_COMMAND
			options: [
				{
					name: "block",
					description: "A ranging block hex dump from a bug report",
					type: 3, // STRING
					required: true,
				},
			],
		},
	],
};

interface SubcommandOption {
	name: string;
	value?: unknown;
	options?: SubcommandOption[];
}

function subcommand(c: CommandContext): { name: string; options: SubcommandOption[] } | undefined {
	const top = c.interaction.data?.options?.[0] as SubcommandOption | undefined;
	if (!top) return undefined;
	return { name: top.name, options: top.options ?? [] };
}

function numberOpt(options: SubcommandOption[], name: string): number | undefined {
	const raw = options.find((o) => o.name === name)?.value;
	return typeof raw === "number" ? raw : undefined;
}

function stringOpt(options: SubcommandOption[], name: string): string | undefined {
	const raw = options.find((o) => o.name === name)?.value;
	return typeof raw === "string" ? raw : undefined;
}

const COVERAGE_NOTE =
	"Covers protocol and crypto-maths only: the real compiled DS-TWR/CCC responder, run " +
	"in this Worker. It proves nothing about PDoA/AoA on the BU04, NFC Express Mode, iOS " +
	"point-release behaviour, or physical approach unlock.";

function fmtCm(v: number | null): string {
	return v === null ? "—" : `${v} cm`;
}

/** One leg of the exchange as `twin_glue.c:143-149`. The anchor line comes from
 *  the drift-checked table; only the end of the span is written here, since
 *  `Citation` is a single line and a range has nothing to re-read. */
function leg(c: { file: string; line: number }, endLine: number): string {
	return `${c.file.split("/").pop()}:${c.line}-${endLine}`;
}

function sequenceDiagram(): string {
	return [
		"```",
		" iPhone (initiator)                DWM3001CDK responder",
		"        |                                   |",
		`        |──── Pre-POLL (SP0, encrypted) ───>|  ${leg(TWIN.legPrePoll, 149)}`,
		"        |                                   |  RX arms the SP3 POLL window",
		`        |<─── POLL (STS-only RFRAME) ───────|  ${leg(TWIN.legPoll, 154)}`,
		`        |──── Response_0 (delayed TX) ─────>|  ${leg(TWIN.legResponse, 158)}`,
		`        |<─── Final (RFRAME + STS qual) ────|  ${leg(TWIN.legFinal, 164)}`,
		`        |──── Final_Data (dUDSK-enc.) ─────>|  ${leg(TWIN.legFinalData, 177)}`,
		"        |                                   |  DS-TWR distance computed + latched",
		"```",
	].join("\n");
}

function roundsTable(result: ApproachResult): string {
	const rows = result.rounds;
	const shown = rows.length <= 10 ? rows : [...rows.slice(0, 5), ...rows.slice(-5)];
	const truncated = rows.length > 10;
	const lines = shown.map((r) => {
		const measured = r.measuredCm === null ? "DROPPED" : `${r.measuredCm} cm`;
		return `  block ${String(r.block).padStart(3)}  truth ${String(r.groundTruthCm).padStart(4)} cm  ` +
			`measured ${measured.padStart(8)}  trust ${r.trustLevelAfter}/${result.trustK}  ` +
			`trusted ${fmtCm(r.trustedCmAfter)}`;
	});
	if (truncated) {
		const cut = rows.length - 10;
		lines.splice(5, 0, `  … ${cut} round(s) omitted …`);
	}
	return "```\n" + lines.join("\n") + "\n```";
}

function formatApproach(result: ApproachResult, opts: { speedMps: number; noise: NoiseLevel; drops: number }): string {
	const parts = [
		`**/twin approach** speed \`${opts.speedMps} m/s\`, noise \`${opts.noise}\`, drops \`${opts.drops}\` ` +
			`— ${result.rounds.length} ranging block(s), ${BLOCK_MS} ms apart:`,
		"",
		"**Exchange (one representative round):**",
		sequenceDiagram(),
		"**Rounds:**",
		roundsTable(result),
		"",
		`**Trust gate: ${result.decision.outcome === "trusted" ? "OPENED" : "did not open"}.** ` +
			result.decision.reason,
		`Decided at \`${result.decision.citation}\` (agreement spread ${FIRA_RANGE_SPREAD_CM} cm).`,
		"",
		"_A PNG plot is not implemented yet; this is the ASCII form the parent prompt names as the fallback._",
		"",
		COVERAGE_NOTE,
	];
	return parts.join("\n");
}

function handleApproach(c: CommandContext): Response {
	const sub = subcommand(c);
	const options = sub?.options ?? [];
	const speedMps = numberOpt(options, "speed") ?? 1.4;
	const noiseRaw = stringOpt(options, "noise") ?? "none";
	const drops = numberOpt(options, "drops") ?? 0;

	if (speedMps < MIN_SPEED || speedMps > MAX_SPEED) {
		return jsonResponse({
			type: InteractionResponseType.ChannelMessageWithSource,
			data: { content: `speed must be between ${MIN_SPEED} and ${MAX_SPEED} m/s.`, flags: 64 },
		});
	}
	if (!NOISE_LEVELS.includes(noiseRaw as NoiseLevel)) {
		return jsonResponse({
			type: InteractionResponseType.ChannelMessageWithSource,
			data: { content: "noise must be none, light, or heavy.", flags: 64 },
		});
	}
	const noise = noiseRaw as NoiseLevel;

	return defer(c, async () => {
		try {
			const handle = await bootTwin();
			const result = runApproachScenario(handle, { speedMps, noise, drops });
			return formatApproach(result, { speedMps, noise, drops });
		} catch (err) {
			if (err instanceof ScenarioTooLong) {
				return (
					`That scenario would take ${err.wouldBeRounds} ranging blocks; ` +
					`/twin approach caps a run at ${MAX_ROUNDS}. Raise the speed, or start closer.`
				);
			}
			throw err;
		}
	});
}

function handleExplain(c: CommandContext): Response {
	const sub = subcommand(c);
	const block = stringOpt(sub?.options ?? [], "block") ?? "";
	const truncated = block.length > 0 ? ` (${Math.min(block.length, 64)} of ${block.length} hex chars read, then stopped)` : "";
	return jsonResponse({
		type: InteractionResponseType.ChannelMessageWithSource,
		data: {
			content:
				"`/twin explain` is not implemented, on purpose, not as a bug: `twin_glue.c` has no " +
				"entry point that decodes a raw wire frame — every call in it synthesizes DS-TWR frames " +
				"from a target distance, and the whole exchange is CCM*-encrypted against the twin's own " +
				"internal test key. A real board's ranging block was encrypted under its own session key, " +
				"so even a raw-bytes entry point could not decrypt it here. Decoding a pasted block would " +
				"need a new, reviewed entry point in `modules/woz_uwb/src/ccc/ccc_shim_rx.c`, which is a " +
				`firmware change and out of scope for this bot.${truncated}`,
			flags: 64,
		},
	});
}

export function handler(c: CommandContext): Response {
	const sub = subcommand(c);
	if (sub?.name === "explain") return handleExplain(c);
	return handleApproach(c);
}
