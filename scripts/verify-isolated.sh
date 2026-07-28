#!/usr/bin/env bash
#
# Candidate-tree verifier for the sandboxed `git pr` helper.
#
# The helper intentionally denies network access, replaces HOME with private
# scratch space, excludes user-local tools from PATH, and clones no gitignored
# .venv. Those boundaries make eight full-sweep gates impossible by construction:
#
#   zizmor, licenses,
#   clang-tidy             user-local pinned tools are outside the clean PATH
#   twin-wasm              emsdk normally lives under the real HOME
#   patch-drift            reads pinned public upstream revisions over network
#   test, coverage         require python modules from the gitignored .venv
#   test-tui               tools/tui/node_modules is gitignored, and restoring it
#                          needs both the network and bun's cache under the real
#                          HOME, so every step of the gate is unreachable
#
# CI still runs all eight. The ordinary developer sweep remains `make verify`;
# this wrapper exists only so the isolated candidate can run every gate its
# declared capabilities actually support without weakening verify.sh's rule
# that an accidentally missing tool is fatal.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# twin-wasm normally runs this before rebuilding. Keep the committed-WASM
# behavior check even though the sandbox cannot discover emcc for the rebuild.
node "$ROOT/web-twin/selftest.cjs"

ISOLATED_SKIP="zizmor licenses clang-tidy twin-wasm patch-drift test coverage test-tui"
export SKIP="${SKIP:+$SKIP }$ISOLATED_SKIP"
exec "$ROOT/scripts/verify.sh"
