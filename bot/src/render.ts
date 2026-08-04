/**
 * @file The compatibility matrix, as a PNG.
 *
 * Satori (JSX-shaped element tree + CSS-subset styles -> SVG) does the text
 * shaping itself and bakes glyphs into vector paths, so the SVG it produces
 * is self-contained — @resvg/resvg-wasm rasterizes it with no font of its
 * own needed. WASM is initialised once per Worker isolate and reused, per
 * the spec this bot follows ("Load WASM once at module init").
 *
 * Colors are exactly the four semantic accents the spec defines for status
 * Containers elsewhere in this bot (0x2ECC71/0xF1C40F/0xE74C3C/0x5865F2):
 * reused here rather than inventing a fifth palette, so "owned" and
 * "nobody owns it" read the same way in the image as they eventually will
 * in a Container.
 */
import satori from "satori";
import { Resvg, initWasm } from "@resvg/resvg-wasm";
import { BOARDS, boardLabel } from "./boards.ts";
import { distinctVersions, type ValidationStatus } from "./matrix.ts";
import type { MatrixCount } from "./rigs.ts";
import { interBoldFont, interRegularFont, resvgWasmBytes } from "./assets.ts";

const COLOR = {
	bg: "#1e1f22",
	panel: "#2b2d31",
	text: "#f2f3f5",
	subtext: "#949ba4",
	border: "#3f4147",
	pass: "#2ECC71",
	unvalidated: "#F1C40F",
	fail: "#E74C3C",
	unowned: "#5865F2",
} as const;

const LABEL_COL_WIDTH = 168;
const DATA_COL_WIDTH = 96;
const ROW_HEIGHT = 52;
const HEADER_HEIGHT = 52;
const TITLE_HEIGHT = 84;
const LEGEND_HEIGHT = 44;
const PADDING = 32;

/** More columns than this and the image stops being screenshot-sized; the
 *  most recent versions are the ones anyone actually asks about. */
const MAX_COLUMNS = 12;

/** The four-item legend is wider than a one- or two-column grid. Found by
 *  actually rendering a narrow matrix and looking at it: at the grid's
 *  natural width, "known-broken" ran off the right edge of a fixed-width
 *  canvas rather than wrapping. This floor is a rendered, eyeballed number,
 *  not a computed one — Satori does not expose text metrics ahead of a
 *  layout pass, so it carries some margin rather than being exact. */
const MIN_WIDTH = 720;

let wasmReady: Promise<void> | undefined;

function ensureWasm(): Promise<void> {
	return (wasmReady ??= initWasm(resvgWasmBytes()));
}

function cellColor(owners: number, status: ValidationStatus | undefined): string {
	if (status) return status.passed ? COLOR.pass : COLOR.fail;
	return owners > 0 ? COLOR.unvalidated : COLOR.unowned;
}

// Satori's element type is a plain, JSX-shaped object tree — no React or
// JSX runtime dependency needed to build one by hand.
type Node = { type: string; props: Record<string, unknown> };
const el = (type: string, props: Record<string, unknown>, children?: unknown): Node => ({
	type,
	props: children === undefined ? props : { ...props, children },
});

function legendDot(color: string, label: string): Node {
	return el("div", { style: { display: "flex", alignItems: "center", marginRight: "20px" } }, [
		el("div", {
			style: { width: "14px", height: "14px", borderRadius: "7px", background: color, marginRight: "8px" },
		}),
		el("span", { style: { fontSize: "18px", color: COLOR.subtext } }, label),
	]);
}

export interface MatrixImageInput {
	counts: readonly MatrixCount[];
	results?: readonly ValidationStatus[];
	generatedAtLabel: string;
}

/** Builds the element tree and the canvas size it needs. Pure and
 *  synchronous — split out from `renderMatrixPng` so layout can be unit
 *  tested without touching WASM. Returns null if there is no column axis
 *  to draw, same condition as `matrixTable`. */
export function buildMatrixImage(
	input: MatrixImageInput,
): { tree: Node; width: number; height: number; truncated: boolean } | null {
	const all = distinctVersions(input.counts);
	if (all.length === 0) return null;
	const truncated = all.length > MAX_COLUMNS;
	const versions = truncated ? all.slice(-MAX_COLUMNS) : all;
	const results = input.results ?? [];

	const countFor = (board: string, v: string): number =>
		input.counts.find((c) => c.board === board && c.ios_version === v)?.n ?? 0;
	const statusFor = (board: string, v: string): ValidationStatus | undefined =>
		results.find((r) => r.board === board && r.iosVersion === v);

	const width = Math.max(MIN_WIDTH, PADDING * 2 + LABEL_COL_WIDTH + versions.length * DATA_COL_WIDTH);
	const height =
		PADDING * 2 + TITLE_HEIGHT + HEADER_HEIGHT + BOARDS.length * ROW_HEIGHT + LEGEND_HEIGHT;

	const headerRow = el(
		"div",
		{ style: { display: "flex", flexDirection: "row", height: `${HEADER_HEIGHT}px` } },
		[
			el("div", { style: { width: `${LABEL_COL_WIDTH}px` } }),
			...versions.map((v) =>
				el(
					"div",
					{
						style: {
							width: `${DATA_COL_WIDTH}px`,
							display: "flex",
							alignItems: "center",
							justifyContent: "center",
							fontSize: "20px",
							fontWeight: 700,
							color: COLOR.text,
						},
					},
					v,
				),
			),
		],
	);

	const rows = BOARDS.map((b) =>
		el(
			"div",
			{
				style: {
					display: "flex",
					flexDirection: "row",
					height: `${ROW_HEIGHT}px`,
					borderTop: `1px solid ${COLOR.border}`,
				},
			},
			[
				el(
					"div",
					{
						style: {
							width: `${LABEL_COL_WIDTH}px`,
							display: "flex",
							alignItems: "center",
							fontSize: "20px",
							fontWeight: 700,
							color: COLOR.text,
						},
					},
					boardLabel(b.value),
				),
				...versions.map((v) => {
					const n = countFor(b.value, v);
					return el(
						"div",
						{
							style: {
								width: `${DATA_COL_WIDTH}px`,
								display: "flex",
								alignItems: "center",
								justifyContent: "center",
							},
						},
						el(
							"div",
							{
								style: {
									display: "flex",
									alignItems: "center",
									justifyContent: "center",
									width: "56px",
									height: "32px",
									borderRadius: "8px",
									background: cellColor(n, statusFor(b.value, v)),
									color: "#1e1f22",
									fontSize: "18px",
									fontWeight: 700,
								},
							},
							String(n),
						),
					);
				}),
			],
		),
	);

	const legend = el(
		"div",
		{ style: { display: "flex", flexDirection: "row", height: `${LEGEND_HEIGHT}px`, alignItems: "center" } },
		[
			legendDot(COLOR.unvalidated, "owned, unvalidated"),
			legendDot(COLOR.unowned, "nobody owns it"),
			legendDot(COLOR.pass, "validated"),
			legendDot(COLOR.fail, "known-broken"),
		],
	);

	const tree = el(
		"div",
		{
			style: {
				display: "flex",
				flexDirection: "column",
				width: `${width}px`,
				height: `${height}px`,
				background: COLOR.bg,
				padding: `${PADDING}px`,
				fontFamily: "Inter",
			},
		},
		[
			el(
				"div",
				{ style: { display: "flex", flexDirection: "column", height: `${TITLE_HEIGHT}px`, justifyContent: "center" } },
				[
					el("div", { style: { fontSize: "28px", fontWeight: 700, color: COLOR.text } }, "openaliro hardware compatibility"),
					el("div", { style: { fontSize: "16px", color: COLOR.subtext, marginTop: "4px" } }, input.generatedAtLabel),
				],
			),
			headerRow,
			el("div", { style: { display: "flex", flexDirection: "column" } }, rows),
			legend,
		],
	);

	return { tree, width, height, truncated };
}

export interface RenderedMatrix {
	png: Uint8Array;
	/** True if there were more iOS-version columns than the image shows —
	 *  the caller has to say so somewhere, since the image itself carries
	 *  no "+N more" marker of its own. */
	truncated: boolean;
}

/** Renders the matrix to PNG bytes, or null if there is nothing to draw
 *  (same condition as the monospace table). Callers are expected to fall
 *  back to `matrixTable` on any thrown error — WASM init and Satori's
 *  layout pass are the two realistic failure points, and neither should
 *  turn `/matrix` into a hard failure when the text version would have
 *  worked. */
export async function renderMatrixPng(input: MatrixImageInput): Promise<RenderedMatrix | null> {
	const built = buildMatrixImage(input);
	if (!built) return null;

	const svg = await satori(built.tree as never, {
		width: built.width,
		height: built.height,
		fonts: [
			{ name: "Inter", data: interRegularFont(), weight: 400, style: "normal" },
			{ name: "Inter", data: interBoldFont(), weight: 700, style: "normal" },
		],
	});

	await ensureWasm();
	const resvg = new Resvg(svg, { fitTo: { mode: "width", value: built.width } });
	return { png: resvg.render().asPng(), truncated: built.truncated };
}
