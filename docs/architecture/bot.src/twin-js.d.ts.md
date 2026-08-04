<!-- generated documentation — edit the source, not this file -->
# `bot/src/twin-js.d.ts`

@file Ambient type for the relative import of ../../web-twin/twin.js.
twin.js is a plain CommonJS file (Emscripten's default UMD output), not a
TypeScript module, and lives outside src/ (it is consumed as-is, never
copied — see twin.ts). This is only a type shape for the bundler's CJS
interop; it says nothing about what twin.js actually does at runtime.
