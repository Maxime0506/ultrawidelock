#!/usr/bin/env bash
# deadcode-codechecker.sh — CodeChecker over the real firmware build.
#
# Same target as deadcode-tidy.sh and the same database, but it runs the Clang
# Static Analyzer as well as clang-tidy, keeps results in a store so two runs can
# be diffed, and writes an HTML report. Use deadcode-tidy.sh for the quick pass;
# use this when you want the cross-translation-unit analyser or a report to read.
#
# It reuses the FILTERED database that deadcode-tidy.sh writes, because the raw
# Zephyr one is GCC-flavoured and clang rejects several of its flags outright.
# Running the tidy script first is therefore not optional, and this checks.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-${ALIRO_BUILD_ROOT:-$ROOT/build}/cdk-matter/firmware}"
FILTER="${2:-modules/woz_dfu|firmware/src}"

command -v CodeChecker >/dev/null || command -v codechecker >/dev/null || {
  echo "ERROR: CodeChecker not on PATH" >&2
  echo "       pip install codechecker" >&2
  exit 1
}
CC="$(command -v CodeChecker 2>/dev/null || command -v codechecker)"

DB="$BUILD/tidy/compile_commands.json"
[ -f "$DB" ] || {
  echo "ERROR: no filtered compile database at ${DB#"$ROOT"/}" >&2
  echo "       Run scripts/deadcode-tidy.sh first — it strips the GCC-only flags" >&2
  echo "       that make clang reject every file in the Zephyr database." >&2
  exit 1
}

OUT="$BUILD/codechecker"
rm -rf "$OUT"
mkdir -p "$OUT"

# NOT --enable-all. On a Zephyr tree that switches on the Altera, modernize and
# CERT-C++ packs, and the generated autoconf.h alone then produces about 20,000
# modernize-macro-to-enum reports -- measured: 24,838 findings, of which 19,807
# were that one checker. A report nobody can read is the same as no report. The
# `sensitive` profile plus the explicit disables below is what leaves signal.
#
# alpha.deadcode.UnreachableCode is the reason this script is worth running at
# all: it is the real unreachable-code analysis, and clang-tidy has no equivalent
# (there is no `bugprone-unreachable-code`, whatever the internet says -- ask for
# it and clang-tidy answers "no checks enabled"). It is an alpha checker, so
# treat its findings as leads. Note CodeChecker spells clangsa checkers WITHOUT
# the `clang-analyzer-` prefix, and aborts the whole run on an unknown name.
# One --file glob per alternative in FILTER. Built as an array because quotes
# produced by a command substitution are not re-parsed by the shell, so the
# obvious sed one-liner silently passes a single broken glob.
file_args=()
IFS='|' read -r -a parts <<<"$FILTER"
for p in "${parts[@]}"; do
  file_args+=(--file "*/$p/*")
done

"$CC" analyze "$DB" \
  --output "$OUT/results" \
  --analyzers clangsa clang-tidy \
  "${file_args[@]}" \
  --enable sensitive \
  --enable deadcode.DeadStores \
  --enable alpha.deadcode.UnreachableCode \
  --disable modernize \
  --disable altera \
  --disable cert-dcl37-c \
  --disable cert-dcl51-cpp \
  --disable bugprone-reserved-identifier \
  --disable bugprone-macro-parentheses \
  --disable bugprone-easily-swappable-parameters \
  --disable readability-identifier-length \
  --disable readability-magic-numbers \
  --disable readability-uppercase-literal-suffix \
  --disable readability-use-concise-preprocessor-directives \
  --disable misc-include-cleaner \
  --jobs "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
  >"$OUT/analyze.log" 2>&1 || true

"$CC" parse "$OUT/results" --export html --output "$OUT/html" \
  >"$OUT/parse.log" 2>&1 || true

# parse exits non-zero when it has findings, which is not a failure of the run.
"$CC" parse "$OUT/results" 2>/dev/null | sed "s|$ROOT/||g" || true

printf '\n  HTML report: %s/index.html\n' "${OUT#"$ROOT"/}/html"
printf '  Raw results: %s\n' "${OUT#"$ROOT"/}/results"
