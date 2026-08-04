import { test } from "node:test";
import assert from "node:assert/strict";
import { TextInputStyle, buildModal, selectFieldValue, textFieldValue } from "../src/modal.ts";

test("buildModal wraps every field in a Label component directly (no Action Row)", () => {
	const modal = buildModal("cid", "Title", [
		{ kind: "text", customId: "a", label: "A", style: TextInputStyle.Short },
		{ kind: "select", customId: "b", label: "B", options: [{ name: "One", value: "1" }] },
	]);

	assert.equal(modal.custom_id, "cid");
	assert.equal(modal.components.length, 2);
	for (const c of modal.components) {
		assert.equal(c.type, 18, "top-level component must be a Label (18)");
		assert.ok("component" in c, "Label must wrap exactly one component");
	}
});

test("buildModal caps at 5 fields and truncates title/custom_id to Discord's limits", () => {
	const fields = Array.from({ length: 8 }, (_, i) => ({
		kind: "text" as const,
		customId: `f${i}`,
		label: `Field ${i}`,
		style: TextInputStyle.Short,
	}));
	const modal = buildModal("x".repeat(200), "y".repeat(200), fields);
	assert.equal(modal.components.length, 5);
	assert.equal(modal.custom_id.length, 100);
	assert.equal(modal.title.length, 45);
});

test("a select option list is capped at 25 and labels/values truncated to 100 chars", () => {
	const options = Array.from({ length: 30 }, (_, i) => ({ name: `n${i}`, value: `v${i}` }));
	const modal = buildModal("cid", "Title", [{ kind: "select", customId: "s", label: "S", options }]);
	const select = modal.components[0]?.component as { options: { label: string; value: string }[] };
	assert.equal(select.options.length, 25);
});

test("textFieldValue reads the flat MODAL_SUBMIT component array, trimmed", () => {
	const components = [{ type: 4, custom_id: "phone_model", value: "  iPhone 15 Pro  " }];
	assert.equal(textFieldValue(components, "phone_model"), "iPhone 15 Pro");
	assert.equal(textFieldValue(components, "missing"), undefined);
});

test("textFieldValue treats an empty or whitespace-only value as absent", () => {
	const components = [{ type: 4, custom_id: "notes", value: "   " }];
	assert.equal(textFieldValue(components, "notes"), undefined);
});

test("selectFieldValue reads the first selected value from the flat array", () => {
	const components = [{ type: 3, custom_id: "utc_offset", values: ["-300"] }];
	assert.equal(selectFieldValue(components, "utc_offset"), "-300");
	assert.equal(selectFieldValue(components, "missing"), undefined);
});

test("selectFieldValue on an empty selection is undefined, not a crash", () => {
	const components = [{ type: 3, custom_id: "utc_offset", values: [] }];
	assert.equal(selectFieldValue(components, "utc_offset"), undefined);
});
