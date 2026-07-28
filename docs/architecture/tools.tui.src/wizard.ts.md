<!-- generated documentation — edit the source, not this file -->
# `tools/tui/src/wizard.ts`

**depends on** [`tools/tui/src/targets.ts`](targets.ts.md), [`tools/tui/src/types.ts`](types.ts.md)  ·  **used by** [`tools/tui/src/app.tsx`](app.tsx.md)

<details><summary>Undocumented (6)</summary>

- `isDestructive`
- `confirmView`
- `wizardBackAction` — tested: :left-arrow back navigation follows the wizard hierarchy without escaping active work@l150
- `targetChoices`
- `homeChoices`
- `wizardView` — tested: :active work overrides every stale option screen@l129; :does not invent pairing or automatic toolchain installation for the standalone esp reader@l70; :does not offer guided flashing from an artifact older than repository source@l59; :each confirmation names the loss that is specific to it@l107; :every destructive confirmation offers the same escape-first shape@l94; :factory reset is offered only on targets whose firmware has the command@l114; :failed work returns to recoverable actions instead of a dead end@l122; :labels the recommended n rf console separately from its silent alternate interface@l47

</details>
