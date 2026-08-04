/**
 * @file Decodes the generated base64 assets into bytes, once per Worker
 * isolate. `atob` is a Web platform global available in both Node's test
 * runner and the Workers runtime, so this needs no environment branching.
 */
import {
	INTER_BOLD_TTF_BASE64,
	INTER_REGULAR_TTF_BASE64,
	RESVG_WASM_BASE64,
} from "./assets.generated.ts";

function decodeBase64(b64: string): ArrayBuffer {
	const binary = atob(b64);
	const bytes = new Uint8Array(binary.length);
	for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
	return bytes.buffer;
}

let interRegular: ArrayBuffer | undefined;
let interBold: ArrayBuffer | undefined;
let resvgWasm: ArrayBuffer | undefined;

export function interRegularFont(): ArrayBuffer {
	return (interRegular ??= decodeBase64(INTER_REGULAR_TTF_BASE64));
}

export function interBoldFont(): ArrayBuffer {
	return (interBold ??= decodeBase64(INTER_BOLD_TTF_BASE64));
}

export function resvgWasmBytes(): ArrayBuffer {
	return (resvgWasm ??= decodeBase64(RESVG_WASM_BASE64));
}
