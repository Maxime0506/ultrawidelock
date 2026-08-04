<!-- generated documentation — edit the source, not this file -->
# `bot/src/wasm-module.d.ts`

@file Ambient type for a static `.wasm` module import.
Wrangler's bundler compiles a `.wasm` import into a `WebAssembly.Module`
ahead of time (the one path workerd allows — see twin.ts). TypeScript has
no built-in type for that import shape.
