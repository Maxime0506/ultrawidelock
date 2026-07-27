#!/usr/bin/env bash
#
# Candidate-tree verifier for the sandboxed `git pr` helper.
#
# The helper intentionally denies network access, replaces HOME with private
# scratch space, excludes user-local tools from PATH, and clones no gitignored
# .venv. Those boundaries make seven full-sweep gates impossible by construction:
#
#   zizmor, licenses,
#   clang-tidy             user-local pinned tools are outside the clean PATH
#   twin-wasm              emsdk normally lives under the real HOME
#   patch-drift            reads pinned public upstream revisions over network
#   test, coverage         require python modules from the gitignored .venv
#
# CI still runs all seven. The ordinary developer sweep remains `make verify`;
# this wrapper exists only so the isolated candidate can run every gate its
# declared capabilities actually support without weakening verify.sh's rule
# that an accidentally missing tool is fatal.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# twin-wasm normally runs this before rebuilding. Keep the committed-WASM
# behavior check even though the sandbox cannot discover emcc for the rebuild.
node "$ROOT/web-twin/selftest.cjs"

ISOLATED_SKIP="zizmor licenses clang-tidy twin-wasm patch-drift test coverage"
export SKIP="${SKIP:+$SKIP }$ISOLATED_SKIP"
exec "$ROOT/scripts/verify.sh"
