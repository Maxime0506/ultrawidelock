/**
 * @file The vocabulary alias table, and query expansion over it.
 *
 * Lives on its own because two experiments need it: the Stage 1 probe, which
 * measured it as the one candidate fix that moves the config stratum, and the
 * independent-set scorer, which has to run the same expansion against questions
 * written by somebody who never saw this table.
 *
 * On overfitting: it would be trivial to read the miss list, write an alias per
 * failing question and report a wonderful number that means nothing. Every entry
 * below is written from the domain's own vocabulary -- a serial port IS a UART,
 * a console IS where logs go -- and not one was added by looking at which
 * questions failed. It is also small enough to read in one screen, which is the
 * honest way to let somebody check that claim.
 */

/** user vocabulary -> words this tree actually uses. Bidirectional in spirit;
 *  only the user->repo direction is needed, since queries are what get expanded. */
export const ALIAS: Record<string, string[]> = {
	serial: ["uart", "console", "tty", "vcom"],
	port: ["uart", "console", "tty"],
	tty: ["uart", "console"],
	com: ["uart", "vcom"],
	plug: ["usb", "cable"],
	usb: ["cdc", "acm"],
	prompt: ["shell"],
	terminal: ["console", "shell", "monitor"],
	logs: ["log", "logging", "rtt"],
	logging: ["log", "rtt", "console"],
	print: ["log", "console"],
	hang: ["fault", "stops", "stuck"],
	hangs: ["fault", "hang"],
	crash: ["fault", "assert", "overflow"],
	stack: ["stack_size", "overflow", "guard"],
	memory: ["ram", "heap", "flash"],
	size: ["flash", "ram", "bytes"],
	bluetooth: ["bt", "ble"],
	ble: ["bt", "bluetooth"],
	pairing: ["bond", "bondable", "commission"],
	radio: ["dw3000", "dw3110", "uwb"],
	distance: ["range", "ranging"],
	unlock: ["approach", "ranging", "lock"],
	thread: ["openthread", "ot"],
	network: ["net", "ipv6", "thread"],
	sleep: ["sed", "poll", "mtd"],
	erase: ["flash-erase", "factory"],
	reset: ["reboot", "sw2"],
	debugger: ["probe", "jlink", "swd"],
	probe: ["jlink", "swd", "cdk_probe"],
	build: ["west", "cmake", "pristine"],
	update: ["dfu", "ota", "fota", "mcuboot"],
	firmware: ["image", "zephyr"],
	credential: ["ursk", "key", "sts"],
	encryption: ["crypto", "psa", "mbedtls"],
};

/**
 * The six entries that could plausibly have been reverse-engineered from the one
 * miss analysed in detail (`cfg-uart-console`: the question says "serial port"
 * and "plug", the file says "console", "RTT", "UART"). Held out separately so
 * the honest question -- does query expansion work, or did I write the answer key
 * into the alias table -- has a number rather than an assurance.
 */
export const SUSPECT = new Set(["serial", "port", "tty", "com", "plug", "terminal"]);

export function expandWith(question: string, skip: Set<string>): string {
	const extra: string[] = [];
	for (const w of question.toLowerCase().match(/[a-z][a-z0-9-]{2,}/g) ?? []) {
		if (ALIAS[w] && !skip.has(w)) extra.push(...ALIAS[w]);
	}
	return extra.length ? `${question} ${[...new Set(extra)].join(" ")}` : question;
}

export const expand = (q: string) => expandWith(q, new Set());
export const expandHeldOut = (q: string) => expandWith(q, SUSPECT);
