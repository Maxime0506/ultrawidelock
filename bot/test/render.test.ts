/**
 * @file Unlike the modal wire format (untestable without live Discord),
 * PNG rendering runs entirely in-process — Satori and @resvg/resvg-wasm
 * both work under plain Node. These tests render real images and check
 * real bytes, not just that the code path doesn't throw.
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildMatrixImage, renderMatrixPng } from "../src/render.ts";
import type { MatrixCount } from "../src/rigs.ts";

const PNG_MAGIC = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function readPngDimensions(png: Uint8Array): { width: number; height: number } {
	// IHDR is always the first chunk, immediately after the 8-byte signature
	// and the 4-byte length + 4-byte "IHDR" type: width and height are the
	// next two big-endian uint32s.
	const buf = Buffer.from(png);
	return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) };
}

test("buildMatrixImage returns null with no data, same condition as matrixTable", () => {
	assert.equal(buildMatrixImage({ counts: [], generatedAtLabel: "x" }), null);
});

test("buildMatrixImage sizes the canvas from the board count and column count", () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 1 }];
	const built = buildMatrixImage({ counts, generatedAtLabel: "x" });
	assert.ok(built);
	assert.ok(built.width > 0 && built.height > 0);
	assert.equal(built.truncated, false);
});

test("buildMatrixImage truncates to the most recent versions past the column cap", () => {
	const counts: MatrixCount[] = Array.from({ length: 20 }, (_, i) => ({
		board: "esp32c6",
		ios_version: `${9 + i}.0`,
		n: 1,
	}));
	const built = buildMatrixImage({ counts, generatedAtLabel: "x" });
	assert.ok(built);
	assert.equal(built.truncated, true);
});

test("renderMatrixPng returns null with no data, without touching WASM", async () => {
	assert.equal(await renderMatrixPng({ counts: [], generatedAtLabel: "x" }), null);
});

test("renderMatrixPng produces a real, valid PNG for a single-cell matrix", async () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 3 }];
	const rendered = await renderMatrixPng({ counts, generatedAtLabel: "generated now" });
	assert.ok(rendered);
	assert.equal(rendered.truncated, false);

	assert.deepEqual(Buffer.from(rendered.png.slice(0, 8)), PNG_MAGIC);

	const built = buildMatrixImage({ counts, generatedAtLabel: "generated now" });
	const dims = readPngDimensions(rendered.png);
	assert.equal(dims.width, built!.width);
	assert.equal(dims.height, built!.height);
});

test("renderMatrixPng grows the canvas with more iOS-version columns, past the legend's minimum width", async () => {
	// A narrow grid (few columns) clamps to MIN_WIDTH so the 4-item legend
	// always fits; growth only shows up once the grid itself would be wider
	// than that floor, which takes 6+ columns at this build's column width.
	const one: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 1 }];
	const seven: MatrixCount[] = Array.from({ length: 7 }, (_, i) => ({
		board: "esp32c6",
		ios_version: `${9 + i}.0`,
		n: 1,
	}));
	const pngOne = await renderMatrixPng({ counts: one, generatedAtLabel: "x" });
	const pngSeven = await renderMatrixPng({ counts: seven, generatedAtLabel: "x" });
	assert.ok(pngOne && pngSeven);
	assert.ok(readPngDimensions(pngSeven.png).width > readPngDimensions(pngOne.png).width);
	assert.equal(pngSeven.truncated, false, "7 columns is still under the 12-column cap");
});

test("renderMatrixPng reports truncated: true past the column cap", async () => {
	const counts: MatrixCount[] = Array.from({ length: 20 }, (_, i) => ({
		board: "esp32c6",
		ios_version: `${9 + i}.0`,
		n: 1,
	}));
	const rendered = await renderMatrixPng({ counts, generatedAtLabel: "x" });
	assert.ok(rendered);
	assert.equal(rendered.truncated, true);
});

test("a narrow grid still fits the full legend (regression: 'known-broken' used to run off the edge)", async () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 1 }];
	const built = buildMatrixImage({ counts, generatedAtLabel: "x" });
	assert.ok(built);
	assert.ok(built.width >= 720, "must be at least the legend's measured minimum width");
});

test("renderMatrixPng honours a validation result by rendering without throwing", async () => {
	const counts: MatrixCount[] = [{ board: "esp32c6", ios_version: "19.1", n: 2 }];
	const rendered = await renderMatrixPng({
		counts,
		results: [{ board: "esp32c6", iosVersion: "19.1", passed: true }],
		generatedAtLabel: "x",
	});
	assert.ok(rendered);
	assert.deepEqual(Buffer.from(rendered.png.slice(0, 8)), PNG_MAGIC);
});
