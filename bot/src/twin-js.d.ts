/**
 * @file Ambient type for the relative import of ../../web-twin/twin.js.
 *
 * twin.js is a plain CommonJS file (Emscripten's default UMD output), not a
 * TypeScript module, and lives outside src/ (it is consumed as-is, never
 * copied — see twin.ts). This is only a type shape for the bundler's CJS
 * interop; it says nothing about what twin.js actually does at runtime.
 */
declare module "*/web-twin/twin.js" {
	interface TwinModuleArg {
		print?: (text: string) => void;
		instantiateWasm?: (
			imports: WebAssembly.Imports,
			successCallback: (instance: WebAssembly.Instance, module?: WebAssembly.Module) => void,
		) => Record<string, never>;
	}

	interface TwinModule {
		_twin_boot(): number;
		_twin_step(cm: number): number;
		_twin_block(cm: number): void;
		_twin_leg(): number;
		_twin_last_cm(): number;
		_twin_trusted_cm(): number;
		_twin_trust_level(): number;
		_twin_trust_k(): number;
		_twin_plausible(cm: number): number;
		_twin_take_latches(): number;
		_twin_awaiting_poll(): number;
		_twin_stat_rxenable(): number;
		_twin_stat_starttx(): number;
		_twin_poll_index(): number;
		_twin_block_no(): number;
	}

	function createTwin(opts: TwinModuleArg): Promise<TwinModule>;
	export default createTwin;
}
