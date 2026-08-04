/**
 * @file Ambient type for a static `.wasm` module import.
 *
 * Wrangler's bundler compiles a `.wasm` import into a `WebAssembly.Module`
 * ahead of time (the one path workerd allows — see twin.ts). TypeScript has
 * no built-in type for that import shape.
 */
declare module "*.wasm" {
	const wasmModule: WebAssembly.Module;
	export default wasmModule;
}
