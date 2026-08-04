/**
 * @file The enums a contributor picks from: board, radio, NFC front-end, and
 * UTC offset. Taken from the hardware axes in the spec this bot follows and
 * from CLAUDE.md's own target table.
 *
 * Board, radio and nfc are enforced as Discord command `choices`, so a
 * well-behaved client never sends anything outside the list. They are
 * re-validated here anyway: choices are a client-side convenience, not a
 * server-side guarantee, and every one of these values reaches a bound SQL
 * parameter and a modal `custom_id`.
 */

export const BOARDS = [
	{ name: "DWM3001CDK", value: "dwm3001cdk" },
	{ name: "nRF5340 DK", value: "nrf5340dk" },
	{ name: "ESP32-S3", value: "esp32s3" },
	{ name: "ESP32-C5", value: "esp32c5" },
	{ name: "ESP32-C6", value: "esp32c6" },
	{ name: "BU04", value: "bu04" },
] as const;

export const RADIOS = [
	{ name: "DW3110 (single antenna)", value: "dw3110" },
	{ name: "DW3220 (dual antenna)", value: "dw3220" },
] as const;

export const NFC_FRONT_ENDS = [
	{ name: "ST25R300", value: "st25r300" },
	{ name: "None", value: "none" },
] as const;

function values(list: readonly { value: string }[]): readonly string[] {
	return list.map((l) => l.value);
}

const BOARD_VALUES = values(BOARDS);
const RADIO_VALUES = values(RADIOS);
const NFC_VALUES = values(NFC_FRONT_ENDS);

export function isKnownBoard(v: string): boolean {
	return BOARD_VALUES.includes(v);
}
export function isKnownRadio(v: string): boolean {
	return RADIO_VALUES.includes(v);
}
export function isKnownNfc(v: string): boolean {
	return NFC_VALUES.includes(v);
}

function label(list: readonly { name: string; value: string }[], v: string): string {
	return list.find((l) => l.value === v)?.name ?? v;
}
export const boardLabel = (v: string): string => label(BOARDS, v);
export const radioLabel = (v: string): string => label(RADIOS, v);
export const nfcLabel = (v: string): string => label(NFC_FRONT_ENDS, v);

/**
 * UTC offsets, in whole hours, UTC-11 through UTC+13. Deliberately not the
 * full -12..+14 range some locales use: a Discord string select caps at 25
 * options, and -11..+13 is exactly 25 — which is also exactly the two edge
 * cases ("UTC+13 and UTC-11") the spec's own verification checklist calls
 * out by name, so this range is very likely what it had in mind rather than
 * an arbitrary truncation.
 */
export const UTC_OFFSETS: readonly { name: string; value: string; minutes: number }[] = Array.from(
	{ length: 25 },
	(_, i) => {
		const hours = i - 11; // -11 .. +13
		const minutes = hours * 60;
		return {
			name: `UTC${hours >= 0 ? "+" : ""}${hours}`,
			value: String(minutes),
			minutes,
		};
	},
);

const UTC_OFFSET_MINUTES = new Set(UTC_OFFSETS.map((o) => o.minutes));

export function isKnownUtcOffsetMinutes(minutes: number): boolean {
	return UTC_OFFSET_MINUTES.has(minutes);
}

/** "19.1" or "19.1.2" — one to three dot-separated numeric components, each
 *  1-2 digits. Matches the examples given for ios_version. */
const IOS_VERSION_PATTERN = /^\d{1,2}(\.\d{1,2}){0,2}$/;

export function isValidIosVersion(v: string): boolean {
	return IOS_VERSION_PATTERN.test(v);
}

export interface AwakeWindow {
	start: number;
	end: number;
}

/** "8-23" -> { start: 8, end: 23 }. Both bounds 0-23; equal bounds ("no
 *  awake window", e.g. always-on infra) are accepted, since nothing about
 *  the format rules that out. */
const AWAKE_WINDOW_PATTERN = /^([0-9]|1[0-9]|2[0-3])-([0-9]|1[0-9]|2[0-3])$/;

export function parseAwakeWindow(v: string): AwakeWindow | null {
	const match = AWAKE_WINDOW_PATTERN.exec(v.trim());
	if (!match) return null;
	return { start: Number(match[1]), end: Number(match[2]) };
}
