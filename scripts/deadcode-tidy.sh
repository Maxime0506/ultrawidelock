#!/usr/bin/env bash
# deadcode-tidy.sh — run clang-tidy against the REAL firmware build.
#
# scripts/verify.sh already has a clang-tidy gate, but it compiles UNIT_SRCS out
# of tests/host/sources.sh with host flags: -std=c11, a macOS sysroot, and the
# host fakes. That covers six modules and nothing else. firmware/src and
# modules/woz_dfu are in none of it, which security/semgrep-parse-baseline.txt
# already records as a gap -- and modules/woz_dfu parses signed update payloads
# arriving over Bluetooth.
#
# This runs the same tool against build/<img>/compile_commands.json instead, so
# the analysis sees the actual Cortex-M4 target, the real include paths and the
# generated autoconf.h, rather than a host approximation of them.
#
# Two things have to be fixed before clang can read a GCC database:
#
#   1. GCC-only flags are hard errors to clang ("unknown argument"), not
#      warnings, so one of them kills the whole file. They are stripped below.
#      The list is deliberately explicit: a silent catch-all would also swallow
#      a flag that changes semantics.
#   2. Zephyr's generated autoconf.h defines negative Kconfig values bare
#      (#define CONFIG_SYSTEM_WORKQUEUE_PRIORITY -1), which trips
#      bugprone-macro-parentheses ~1000 times per file. The header filter keeps
#      findings to this repo's own sources.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-${ALIRO_BUILD_ROOT:-$ROOT/build}/cdk-matter/firmware}"
FILTER="${2:-modules/woz_dfu|firmware/src}"

command -v clang-tidy >/dev/null || { echo "ERROR: clang-tidy not on PATH" >&2; exit 1; }
DB="$BUILD/compile_commands.json"
[ -f "$DB" ] || {
  echo "ERROR: no compile database at ${DB#"$ROOT"/}" >&2
  echo "       Run: make build   (Zephyr writes it next to the image)" >&2
  exit 1
}

# Flags GCC takes and clang rejects outright. Verified against this database:
# without stripping these, every file fails with clang-diagnostic-error before
# a single check runs.
DROP='^(-fno-printf-return-value|-fno-reorder-functions|-mfp16-format=.*|-fno-defer-pop|-fcheck-new|-mtp=.*|-specs=.*|--param=.*|-flto=auto|-fmacro-prefix-map=.*|-fdiagnostics-color=.*)$'

OUT="$BUILD/tidy"
mkdir -p "$OUT"
DROP="$DROP" python3 - "$DB" "$OUT/compile_commands.json" <<'PY'
import json, os, re, sys
src, dst = sys.argv[1], sys.argv[2]
drop = re.compile(os.environ["DROP"])
db = json.load(open(src))
out = []
for e in db:
    toks = e["command"].split()
    # .S files go through the assembler; clang-tidy has nothing to say about them.
    if e["file"].endswith((".S", ".s")):
        continue
    e = dict(e, command=" ".join(t for t in toks if not drop.match(t)))
    out.append(e)
json.dump(out, open(dst, "w"))
print(f"  filtered database: {len(out)} entries (from {len(db)})")
PY

# Which files to analyse. The database carries every Zephyr and vendor unit; we
# only want the ones this repo owns and the host gate never reaches.
# Read into an array the portable way: macOS ships bash 3.2, which has no mapfile.
files=()
while IFS= read -r f; do
  [ -n "$f" ] && files+=("$f")
done < <(python3 - "$OUT/compile_commands.json" "$ROOT" "$FILTER" <<'PY'
import json, re, sys
db, root, filt = json.load(open(sys.argv[1])), sys.argv[2], re.compile(sys.argv[3])
seen = set()
for e in db:
    f = e["file"]
    if not f.startswith(root + "/"):
        continue                      # out-of-tree entirely
    rel = f[len(root) + 1:]
    # The fetched NCS lives at workspace/ INSIDE the repo root, and its own tree
    # has directories called modules/ and drivers/. A substring match on the
    # relative path therefore drags in all of Zephyr: a filter of "modules/"
    # selected 444 files instead of 63. Upstream is not ours to analyse.
    if rel.startswith(("workspace/", "build/")):
        continue
    if filt.search(rel) and rel not in seen:
        seen.add(rel)
        print(f)
PY
)

if [ ${#files[@]} -eq 0 ]; then
  echo "  no sources matching /$FILTER/ in the database — nothing to analyse" >&2
  exit 1
fi
printf '  analysing %d file(s) matching /%s/\n\n' "${#files[@]}" "$FILTER"

# -Wunused-function is how static-but-never-called is actually reported; it is a
# compiler diagnostic, not a check name. `clang-diagnostic-unused-function`
# cannot be passed to --checks -- clang-tidy answers "no checks enabled".
raw="$OUT/clang-tidy.out"
# C++ translation units need GCC's libstdc++ headers, which the database does not
# name: g++ finds them implicitly from its own install, clang cannot. Point clang
# at the toolchain so <cstdint> resolves. Harmless for the C units.
# Extracted with python, not `sed | head`: head closes the pipe on the first
# line, sed takes SIGPIPE, and `set -o pipefail` then kills this script with 141
# before a single file is analysed.
sysroot="$(python3 - "$DB" <<'PY' || true
import json, re, sys
for e in json.load(open(sys.argv[1])):
    m = re.search(r'--sysroot=(\S+)', e["command"])
    if m:
        print(m.group(1))
        break
PY
)"
extra_tc=()
if [ -n "$sysroot" ]; then
  # g++ knows its own libstdc++ location; clang driving a GCC sysroot does not,
  # and the database never spells it out. Without these the C++ units all die on
  # "'cstdint' file not found" and analyse nothing.
  for d in "$sysroot"/include/c++/*/; do
    [ -d "$d" ] || continue
    extra_tc+=(--extra-arg="-isystem${d%/}")
    [ -d "$d/arm-zephyr-eabi" ] && extra_tc+=(--extra-arg="-isystem${d%/}/arm-zephyr-eabi")
  done
fi

clang-tidy -p "$OUT" \
  --checks='-*,clang-analyzer-deadcode.DeadStores,clang-analyzer-core.*,bugprone-*,-bugprone-macro-parentheses,-bugprone-easily-swappable-parameters,-bugprone-assignment-in-if-condition,misc-unused-parameters,misc-unused-alias-decls' \
  --header-filter="$ROOT/(modules|firmware|ports)/.*" \
  --extra-arg=-Wunused-function \
  --extra-arg=-Wunused-variable \
  --extra-arg=-Wno-unknown-warning-option \
  ${extra_tc+"${extra_tc[@]}"} \
  --quiet "${files[@]}" >"$raw" 2>&1 || true

sed "s|$ROOT/||g" "$raw" | grep -vE '^[0-9]+ (warning|error)s? generated' || true

# A translation unit that failed to compile produced NO analysis, and clang-tidy
# still exits 0. Reporting that as "no findings" is the same lie as a gate that
# cannot fail -- it is how this script once printed a clean 0 for a run in which
# all 12 files errored on a missing C++ header. Count them and say so.
failed="$(grep -c '^Error while processing' "$raw" || true)"
if [ "${failed:-0}" -gt 0 ]; then
  printf '\n  !! %d of %d file(s) FAILED TO COMPILE and were NOT analysed.\n' \
    "$failed" "${#files[@]}" >&2
  printf '     No findings from those files means nothing was checked, not that\n' >&2
  printf '     they are clean. First error:\n' >&2
  grep -m1 'error:' "$raw" | sed "s|$ROOT/||g;s|^|       |" >&2
  exit 1
fi
printf '\n  %d file(s) analysed, all compiled.\n' "${#files[@]}"
