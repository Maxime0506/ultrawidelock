<!-- generated documentation — edit the source, not this file -->
# `bot/src/assets.ts`

@file Decodes the generated base64 assets into bytes, once per Worker
isolate. `atob` is a Web platform global available in both Node's test
runner and the Workers runtime, so this needs no environment branching.

**depends on** [`bot/src/assets.generated.ts`](assets.generated.ts.md)  ·  **used by** [`bot/src/render.ts`](render.ts.md)

<details><summary>Undocumented (4)</summary>

- `decodeBase64`
- `interRegularFont`
- `interBoldFont`
- `resvgWasmBytes`

</details>
