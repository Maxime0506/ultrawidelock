#!/usr/bin/env bash
# deadcode-graph.sh — find functions nothing calls, using the documate code graph.
#
# Why this exists rather than -Wl,--print-gc-sections: that flag lists what the
# linker THREW AWAY, which by definition is not in flash. The dead code worth
# finding is what survives gc-sections because something references it without
# ever calling it -- a function in an ops table, a callback registered into a
# struct nobody dispatches. deps/dw3000's interface_rx_enable was exactly that:
# present in the shipped ELF, zero callers, kept because a dwt_mcps_ops table
# names it. No linker flag can see that; a call graph can.
#
# Three tiers, because "the graph shows no callers" is not evidence of death:
#
#   A  zero inbound CALLS, referenced nowhere else in the tree, AND absent from
#      the unindexed upstream. The only tier worth calling a candidate.
#   B  zero inbound CALLS, but referenced somewhere in-tree -- an ops table, a
#      SYS_INIT/SHELL_CMD registration, a header declaration. Zephyr registers
#      through linker arrays constantly, so most of tier B is alive. This is NOT
#      a delete list; it is where table-registered dead code hides, and reading
#      the reference is the only way to tell which.
#   U  zero inbound CALLS in-tree, but the fetched upstream calls it. Live API.
#
# Tier U exists because the first version of this script did not have it and
# proposed deleting nine woz_aliro_stack methods -- the module reimplements the
# Nordic Aliro API, and every one of them is called from
# workspace/ncs-door-lock-and-access-control, which documate does not index.
# CLAUDE.md warns about exactly this: fetched upstream is not in the graph.
# Without a workspace to check, tier A is unverifiable and says so.
#
# Needs .documate/graph.db, which `make docs` builds and .gitignore excludes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="${DOCUMATE_DB:-$ROOT/.documate/graph.db}"
TIER="${1:-A}"

[ -f "$DB" ] || {
  echo "ERROR: no code graph at ${DB#"$ROOT"/}" >&2
  echo "       Run: make docs   (it is gitignored, so a fresh clone has none)" >&2
  exit 1
}
command -v sqlite3 >/dev/null || { echo "ERROR: sqlite3 not on PATH" >&2; exit 1; }

case "$TIER" in
  A | B | U | both | all) ;;
  *) echo "usage: deadcode-graph.sh [A|B|U|both|all]" >&2; exit 2 ;;
esac

# Trees that call into this repo but are not in the code graph. Anything found
# here is live no matter what the graph says.
upstream=()
[ -d "$ROOT/workspace" ] && upstream+=("$ROOT/workspace")
[ -n "${ESP_MATTER_PATH:-}" ] && [ -d "$ESP_MATTER_PATH" ] && upstream+=("$ESP_MATTER_PATH")
[ -n "${IDF_PATH:-}" ] && [ -d "$IDF_PATH" ] && upstream+=("$IDF_PATH")

# Entry points, and shapes invoked by generated or vendor code rather than by
# any C call site we could see.
is_entry_point() {
  case "$1" in
    main | app_main | *_main) return 0 ;;
    emberAf* | Matter* | chip*) return 0 ;;
    *) return 1 ;;
  esac
}

# Every C/C++ function that is never the target of a CALLS edge. Targets are
# mostly bare names (about 24k of 31k), so match on name and also strip the
# qualifier off the ones that carry one.
candidates="$(sqlite3 "$DB" "
WITH called AS (
  SELECT DISTINCT target_qualified AS n FROM edges WHERE kind='CALLS'
  UNION
  SELECT DISTINCT substr(target_qualified, instr(target_qualified,'::')+2)
    FROM edges WHERE kind='CALLS' AND target_qualified LIKE '%::%'
)
SELECT file_path || '|' || COALESCE(line_start,0) || '|' || name
  FROM nodes
 WHERE kind='Function' AND is_test=0 AND language IN ('c','cpp')
   AND name NOT IN (SELECT n FROM called)
 ORDER BY file_path, line_start;")"

shortlist=(); b_out=""; n_b=0; n_skip=0
while IFS='|' read -r path line name; do
  [ -n "${name:-}" ] || continue
  rel="${path#"$ROOT"/}"
  # Graph rows outlive the files they describe (the db is rebuilt, not pruned,
  # and this tree deletes files). A stale row is not a finding.
  [ -f "$ROOT/$rel" ] || { n_skip=$((n_skip + 1)); continue; }
  if is_entry_point "$name"; then n_skip=$((n_skip + 1)); continue; fi

  # One mention in a table or a macro is enough to keep a symbol in the image,
  # which is the whole reason A and B differ.
  refs="$(git -C "$ROOT" grep -Fw --no-color -c -- "$name" -- \
    ':!*.md' ':!docs' ':!.documate' 2>/dev/null |
    awk -F: '{ s += $NF } END { print s + 0 }')"
  if [ "$refs" -le 1 ]; then
    shortlist+=("$rel|$line|$name")
  else
    b_out="$b_out  $rel:$line  $name  ($refs mentions)"$'\n'; n_b=$((n_b + 1))
  fi
done <<<"$candidates"

# Only the shortlist pays for the upstream scan: it is seconds per symbol across
# a multi-GB tree, and pointless for anything already referenced in-tree.
a_out=""; u_out=""; n_a=0; n_u=0
for row in ${shortlist+"${shortlist[@]}"}; do
  IFS='|' read -r rel line name <<<"$row"
  hit=""
  if [ ${#upstream[@]} -gt 0 ]; then
    hit="$(grep -rlw --include='*.c' --include='*.h' --include='*.cpp' \
      --include='*.hpp' --include='*.cc' --include='*.cxx' \
      -- "$name" "${upstream[@]}" 2>/dev/null | head -1 || true)"
  fi
  if [ -n "$hit" ]; then
    u_out="$u_out  $rel:$line  $name  <- ${hit##*/}"$'\n'; n_u=$((n_u + 1))
  else
    a_out="$a_out  $rel:$line  $name"$'\n'; n_a=$((n_a + 1))
  fi
done

if [ "$TIER" = A ] || [ "$TIER" = both ] || [ "$TIER" = all ]; then
  printf '\n  Tier A — no callers, no references, not in upstream (%d)\n\n' "$n_a"
  [ -n "$a_out" ] && printf '%s' "$a_out" || printf '  (none)\n'
  if [ ${#upstream[@]} -eq 0 ]; then
    printf '\n  WARNING: no workspace/ and no ESP_MATTER_PATH, so nothing checked the\n'
    printf '  upstream that calls into this repo. Tier A is UNVERIFIED -- run\n'
    printf '  `make ws-seed` before trusting it. Deleting on this output alone has\n'
    printf '  already been wrong once.\n'
  fi
  # ESP-IDF pulls its dependencies at build time into an app-local
  # managed_components/, so a hook implemented for one of them has its only
  # caller in a directory that does not exist until an ESP build has run. That
  # is how usbd_app_driver_get_cb (a TinyUSB weak override, declared in
  # ports/esp32/components/piv_ccid/idf_component.yml) reached tier A.
  if printf '%s' "$a_out" | grep -q 'ports/esp32' &&
    ! find "$ROOT/ports/esp32" -maxdepth 4 -type d -name managed_components -print -quit |
      grep -q .; then
    printf '\n  NOTE: tier A names something under ports/esp32 and no managed_components/\n'
    printf '  exists here. ESP-IDF dependencies are fetched at build time, so a hook\n'
    printf '  they call looks uncalled. Check its idf_component.yml before believing it.\n'
  fi
fi
if [ "$TIER" = B ] || [ "$TIER" = both ] || [ "$TIER" = all ]; then
  printf '\n  Tier B — no callers, but referenced in-tree (%d) — REVIEW, not a delete list\n\n' "$n_b"
  [ -n "$b_out" ] && printf '%s' "$b_out" || printf '  (none)\n'
fi
if [ "$TIER" = U ] || [ "$TIER" = all ]; then
  printf '\n  Tier U — no in-tree callers, but the fetched upstream calls it: LIVE (%d)\n\n' "$n_u"
  [ -n "$u_out" ] && printf '%s' "$u_out" || printf '  (none)\n'
fi
printf '\n  %d skipped (entry points, or graph rows whose file is gone)' "$n_skip"
printf '  ·  %d upstream tree(s) checked\n' "${#upstream[@]}"
