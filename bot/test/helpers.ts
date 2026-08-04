/**
 * @file Test-only Ed25519 helpers.
 *
 * The tests mint their own keypair rather than carrying a fixture, so no key
 * material of any kind sits in the repository and the suite cannot be
 * weakened by someone rotating a checked-in value.
 */

export function toHex(bytes: Uint8Array): string {
	return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

export interface TestKey {
	privateKey: CryptoKey;
	publicKeyHex: string;
}

export async function makeKey(): Promise<TestKey> {
	const pair = (await crypto.subtle.generateKey({ name: "Ed25519" }, true, [
		"sign",
		"verify",
	])) as CryptoKeyPair;
	const raw = new Uint8Array(
		(await crypto.subtle.exportKey("raw", pair.publicKey)) as ArrayBuffer,
	);
	return { privateKey: pair.privateKey, publicKeyHex: toHex(raw) };
}

/** Sign exactly what Discord signs: timestamp concatenated with the raw body. */
/** Takes either the whole `TestKey` or just its `privateKey`. The two halves of
 *  this bot each grew their own convention, and accepting both is cheaper than
 *  rewriting every call site in either suite to match the other. */
export async function signBody(
	key: TestKey | CryptoKey,
	timestamp: string,
	body: string,
): Promise<string> {
	const privateKey = "privateKey" in key ? key.privateKey : key;
	const sig = await crypto.subtle.sign(
		{ name: "Ed25519" },
		privateKey,
		new TextEncoder().encode(timestamp + body),
	);
	return toHex(new Uint8Array(sig));
}

/** Flip one bit in a hex string, keeping its length and hex-ness intact. */
export function corrupt(hex: string): string {
	const first = Number.parseInt(hex[0]!, 16);
	return ((first ^ 0x1) & 0xf).toString(16) + hex.slice(1);
}

/** A POST shaped like Discord's, with whatever headers the caller supplies. */
export function interactionRequest(
	body: string,
	headers: Record<string, string>,
): Request {
	return new Request("https://bot.example/", { method: "POST", body, headers });
}

/**
 * An ExecutionContext that remembers what was handed to waitUntil, so a test
 * can await the deferred half of a command instead of guessing at a timeout.
 */
export function makeExecutionContext(): {
	ctx: ExecutionContext;
	settled: () => Promise<unknown>;
} {
	const pending: Promise<unknown>[] = [];
	const ctx = {
		waitUntil(p: Promise<unknown>) {
			pending.push(p);
		},
		passThroughOnException() {},
		props: {},
	} as unknown as ExecutionContext;
	return { ctx, settled: () => Promise.all(pending) };
}

export interface Followup {
	url: string;
	method: string;
	body: {
		content?: string;
		name?: string;
		allowed_mentions?: { parse: string[]; users?: string[] };
		message?: { content?: string; allowed_mentions?: { parse: string[]; users?: string[] } };
		/** The compatibility half posts Components V2 cards and image
		 *  attachments, neither of which is a plain `content` string. */
		attachments?: { id: string; filename: string }[];
		flags?: number;
		components?: unknown[];
	};
	/** Set when the follow-up was sent as multipart with a file (a `/matrix`
	 *  PNG), rather than as JSON. */
	file?: { filename: string; bytes: Uint8Array };
}

export const FAKE_THREAD_ID = "555000000000000001";

export interface CaptureOptions {
	/** Make forum thread creation fail, for the degradation cases. */
	failThreadCreate?: boolean;
}

/**
 * Intercept every Discord call the Worker makes: the follow-up PATCH, the
 * forum thread POST and the in-thread message POST. Nothing leaves the
 * process, and no token is involved because nothing real is contacted.
 */
export function captureFollowups(opts: CaptureOptions = {}): {
	calls: Followup[];
	restore: () => void;
} {
	const calls: Followup[] = [];
	const original = globalThis.fetch;
	let messageCounter = 0;

	globalThis.fetch = (async (input: RequestInfo | URL, init?: RequestInit) => {
		const url = String(input);

		// A follow-up carrying a file is sent as multipart, not JSON: the JSON
		// half arrives as the `payload_json` part and the bytes as `files[0]`.
		// Parsing only the JSON shape here is what made every `/matrix` test
		// fail with "[object FormData] is not valid JSON".
		const raw = init?.body;
		let body: Followup["body"] = {};
		let file: Followup["file"];
		if (typeof FormData !== "undefined" && raw instanceof FormData) {
			body = JSON.parse(String(raw.get("payload_json") ?? "{}"));
			const part = raw.get("files[0]");
			if (part && typeof part !== "string") {
				file = {
					filename: part.name,
					bytes: new Uint8Array(await part.arrayBuffer()),
				};
			}
		} else {
			body = JSON.parse(String(raw ?? "{}"));
		}

		calls.push({ url, method: init?.method ?? "GET", body, ...(file ? { file } : {}) });

		if (url.endsWith("/threads")) {
			if (opts.failThreadCreate) return new Response("forbidden", { status: 403 });
			return new Response(JSON.stringify({ id: FAKE_THREAD_ID }), { status: 200 });
		}

		// discordRest.ts's postMessage reads `channel_id` and `id` back off a
		// channel message POST and stores both; every other caller here ignores
		// the response body. Returning a bare `{id}` made those bind undefined.
		const channelPost = /\/channels\/([^/]+)\/messages$/.exec(url);
		if (channelPost && (init?.method ?? "") === "POST") {
			messageCounter += 1;
			return new Response(
				JSON.stringify({ id: `msg-${messageCounter}`, channel_id: channelPost[1] }),
				{ status: 200 },
			);
		}

		return new Response(JSON.stringify({ id: "1" }), { status: 200 });
	}) as typeof fetch;

	return { calls, restore: () => void (globalThis.fetch = original) };
}

/** The calls that created a forum thread. */
export function threadCreations(calls: Followup[]): Followup[] {
	return calls.filter((c) => c.url.endsWith("/threads"));
}

/** The calls that posted into a thread. */
export function threadMessages(calls: Followup[]): Followup[] {
	return calls.filter((c) => /\/channels\/\d+\/messages$/.test(c.url));
}

// Aliases kept so the compatibility half's tests, written against its own
// helper names, resolve against this one merged module rather than a second
// copy of the same two functions.
export type TestKeypair = TestKey;
export const generateTestKeypair = makeKey;
