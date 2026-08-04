/**
 * @file `/spec` at the command level.
 *
 * The unit-level scanning is covered by spec-index.test.ts; this checks the
 * one property that matters for the non-goal "never spec text": nothing this
 * command ever returns contains prose from the cited lines, only the file and
 * the line number.
 */
import { strict as assert } from "node:assert";
import { describe, it } from "node:test";
import { normaliseSection } from "../src/commands/spec.ts";
import { SPEC_CITATIONS } from "../src/spec-index.generated.ts";
import worker from "../src/index.ts";
import type { Env } from "../src/env.ts";
import { interactionRequest, makeExecutionContext, makeKey, signBody } from "./helpers.ts";

const TS = "1700000000";

async function invoke(section: string) {
	const key = await makeKey();
	const body = JSON.stringify({
		id: "i1",
		type: 2,
		data: { name: "spec", options: [{ name: "section", value: section }] },
		member: { user: { id: "222222222222222222" } },
	});
	const req = interactionRequest(body, {
		"content-type": "application/json",
		"x-signature-timestamp": TS,
		"x-signature-ed25519": await signBody(key, TS, body),
	});
	const { ctx } = makeExecutionContext();
	const res = await worker.fetch(req, { DISCORD_PUBLIC_KEY: key.publicKeyHex } as Env, ctx);
	return (await res.json()) as { data: { content: string } };
}

describe("normaliseSection", () => {
	it("accepts dotted numbers and table references", () => {
		assert.equal(normaliseSection("14"), "14");
		assert.equal(normaliseSection("11.3.1"), "11.3.1");
		assert.equal(normaliseSection("§14"), "14");
		assert.equal(normaliseSection("table 8-3"), "Table 8-3");
		assert.equal(normaliseSection("Table 8-3"), "Table 8-3");
	});

	it("rejects anything that is not one of those shapes", () => {
		for (const bad of ["", "fourteen", "14.", "table", "'; DROP TABLE registry; --"]) {
			assert.equal(normaliseSection(bad), null, bad);
		}
	});
});

/**
 * The `file:line` a section resolves to, read from the index rather than
 * transcribed. A literal here would be a second place a line number lives, and
 * this one regenerates on every `npm run spec-index` — so a transcribed number
 * turns a routine docs edit into a test failure that looks like a bug.
 */
function citeFor(section: string, file: string): string {
	const found = SPEC_CITATIONS.find((c) => c.section === section && c.file === file);
	assert.ok(found, `the index has no entry for section ${section} in ${file}`);
	return `${found.file}:${found.line}`;
}

describe("/spec", () => {
	it("finds a known section", async () => {
		const reply = await invoke("14");
		assert.ok(reply.data.content.includes(citeFor("14", "docs/esp32-gotchas.md")));
	});

	it("finds a subsection when the parent is queried", async () => {
		const expected = citeFor("11.7.3.4.1", "docs/power-profile.md");
		const reply = await invoke("11.7.3.4.1");
		assert.ok(reply.data.content.includes(expected));
		// Also findable by its top-level section.
		const parent = await invoke("11");
		assert.ok(parent.data.content.includes(expected));
	});

	it("says no citation exists rather than fabricating one", async () => {
		const reply = await invoke("999.999");
		assert.match(reply.data.content, /No file in this repository cites/);
	});

	it("rejects malformed input without echoing it", async () => {
		const reply = await invoke("<script>nope</script>");
		assert.ok(!reply.data.content.includes("<script>"));
		assert.match(reply.data.content, /not a section number/);
	});

	it("never returns spec prose, only file:line pointers", async () => {
		// Every citation this command can ever surface. None of its output
		// should contain anything longer than a path and a colon-number.
		for (const c of SPEC_CITATIONS.slice(0, 5)) {
			const reply = await invoke(c.section);
			for (const line of reply.data.content.split("\n")) {
				if (!line.startsWith("- ")) continue;
				assert.match(line, /^- `docs\/[\w./-]+:\d+` cites §/);
			}
		}
	});
});
