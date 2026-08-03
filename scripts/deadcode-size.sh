#!/usr/bin/env bash
# deadcode-size.sh — flash cost of the functions nothing calls.
#
# deadcode-graph.sh answers "what has no callers". This answers "and what does
# that cost", by joining that list against the symbol sizes in the linked image.
# A zero-caller function that the linker already discarded costs nothing and is
# not worth an argument; one that survived into .text is real flash.
#
# That distinction is the whole reason -Wl,--print-gc-sections is the wrong tool
# for this: it lists what was REMOVED. What is in the image and unreachable never
# appears in its output.
#
#   ./scripts/deadcode-size.sh          rank uncalled symbols by flash bytes
#   ./scripts/deadcode-size.sh --serve  puncover's interactive view instead
#
# puncover renders callers/callees and stack depth per symbol from the DWARF,
# which is worth more than any text report once you are chasing a specific
# function. It is a server: it does not exit, so it is not scriptable. Its
# --generate-report writes stack-usage entries only, not symbol sizes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ALIRO_BUILD_ROOT:-$ROOT/build}/cdk-matter/firmware"
ELF="${ALIRO_ELF:-$BUILD/zephyr/zephyr.elf}"
DB="${DOCUMATE_DB:-$ROOT/.documate/graph.db}"

[ -f "$ELF" ] || { echo "ERROR: no image at ${ELF#"$ROOT"/} — run: make build" >&2; exit 1; }

sdk_bin="$(ls -d /opt/nordic/ncs/toolchains/*/opt/zephyr-sdk/arm-zephyr-eabi/bin 2>/dev/null | head -1 || true)"
NM="${NM:-$sdk_bin/arm-zephyr-eabi-nm}"

if [ "${1:-}" = "--serve" ]; then
  command -v puncover >/dev/null || { echo "ERROR: puncover not on PATH (pip install puncover)" >&2; exit 1; }
  echo "  puncover on http://127.0.0.1:5000 — Ctrl-C to stop (it does not exit on its own)"
  exec puncover --gcc-tools-base "$sdk_bin/arm-zephyr-eabi-" \
    --elf "$ELF" --src_root "$ROOT" --build_dir "$BUILD"
fi

[ -x "$NM" ] || { echo "ERROR: no arm-zephyr-eabi-nm (set NM=)" >&2; exit 1; }
[ -f "$DB" ] || { echo "ERROR: no code graph — run: make docs" >&2; exit 1; }

# Names with no inbound CALLS edge, same predicate deadcode-graph.sh uses.
uncalled="$(sqlite3 "$DB" "
WITH called AS (
  SELECT DISTINCT target_qualified AS n FROM edges WHERE kind='CALLS'
  UNION
  SELECT DISTINCT substr(target_qualified, instr(target_qualified,'::')+2)
    FROM edges WHERE kind='CALLS' AND target_qualified LIKE '%::%'
)
SELECT DISTINCT name FROM nodes
 WHERE kind='Function' AND is_test=0 AND language IN ('c','cpp')
   AND name NOT IN (SELECT n FROM called);")"

# t/T are text (code). Sizes are decimal so awk can total them.
"$NM" --print-size --size-sort --radix=d "$ELF" 2>/dev/null |
  awk '$3 == "t" || $3 == "T" { print $2, $4 }' |
  sort -k2,2 >"${TMPDIR:-/tmp}/dc_sizes.$$"
printf '%s\n' "$uncalled" | sort -u >"${TMPDIR:-/tmp}/dc_names.$$"

printf '\n  In the linked image and never called, by flash bytes\n'
printf '  (survived --gc-sections because something REFERENCES them: an ops\n'
printf '   table, a linker-array registration. Review, not a delete list.)\n\n'

# Sort before formatting: sorting awk's output would drag the summary line into
# the middle of the table.
joined="${TMPDIR:-/tmp}/dc_join.$$"
join -1 2 -2 1 "${TMPDIR:-/tmp}/dc_sizes.$$" "${TMPDIR:-/tmp}/dc_names.$$" |
  sort -k2,2 -rn >"$joined"

awk -v limit="${DEADCODE_LIMIT:-25}" \
  'NR <= limit { printf "  %6d B  %s\n", $2, $1 }
   { n++; total += $2 }
   END { printf "\n  %d symbols, %d B of .text", n, total
         if (n > limit) printf "  (top %d shown; DEADCODE_LIMIT=0 for all)", limit
         printf "\n" }' "$joined"

rm -f "$joined"

rm -f "${TMPDIR:-/tmp}/dc_sizes.$$" "${TMPDIR:-/tmp}/dc_names.$$"
