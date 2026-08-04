/**
 * @file The extraction logic, separate from the file I/O in
 * build-size-baseline.ts so size-baseline.test.ts can drive it against a
 * fresh read without importing a script that writes files as a side effect.
 */
import type { SizeBaseline } from "../src/size-baseline.ts";

/** Same shape check `src/size-baseline.ts` does at runtime, reused so the
 *  generator and the (unused, but retained as a fallback contract) runtime
 *  reader agree on what "parseable" means. */
export function extractPrimary(raw: unknown): SizeBaseline | null {
	if (typeof raw !== "object" || raw === null) return null;

	const doc = raw as {
		primary?: string;
		baselines?: Record<
			string,
			{
				commit?: string;
				config?: { board?: string; extra_conf_file?: string };
				regions?: {
					FLASH?: { size: number; used: number; free: number; pct: number };
					RAM?: { size: number; used: number; free: number; pct: number };
				};
			}
		>;
	};

	const key = doc.primary;
	const entry = key ? doc.baselines?.[key] : undefined;
	if (
		!key ||
		!entry?.regions?.FLASH ||
		!entry.regions.RAM ||
		!entry.commit ||
		!entry.config?.board
	) {
		return null;
	}

	// Picked field by field rather than spread: the source objects carry
	// origin/used_by_sections/load_images/padding too, which nothing here
	// reads, and copying them would both widen RegionUsage for no reason and
	// silently re-fatten the generated file the next time the source schema
	// grows a field.
	const pick = (r: { size: number; used: number; free: number; pct: number }) => ({
		size: r.size,
		used: r.used,
		free: r.free,
		pct: r.pct,
	});

	return {
		config: key,
		commit: entry.commit,
		board: entry.config.board,
		extraConfFile: entry.config.extra_conf_file ?? "(none)",
		regions: { FLASH: pick(entry.regions.FLASH), RAM: pick(entry.regions.RAM) },
	};
}
