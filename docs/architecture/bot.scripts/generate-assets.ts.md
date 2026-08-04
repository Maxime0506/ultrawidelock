<!-- generated documentation — edit the source, not this file -->
# `bot/scripts/generate-assets.ts`

@file Regenerates src/assets.generated.ts from the vendored font files and
the installed @resvg/resvg-wasm's WASM binary.
Both need to reach the Worker as bytes, and need to reach `node --test`
as the same bytes with no bundler involved — Wrangler can import `.wasm`
and (with a module rule) arbitrary binary files natively, but Node's
loader understands neither, and this bot's tests run the TypeScript
sources directly. Base64-embedding sidesteps that split entirely: no
import rule to keep in sync between environments, no bundler-vs-`node
--test` skew. The cost is a large generated file (~1.7 MB gzipped, well
under Cloudflare's 3 MB free-tier compressed Worker limit, confirmed
against developers.cloudflare.com/workers/platform/limits on 2026-08-04).
Run after upgrading @resvg/resvg-wasm or the vendored fonts:
npm run generate-assets

<details><summary>Undocumented (1)</summary>

- `base64Of`

</details>
