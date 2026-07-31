#!/usr/bin/env bash
#
# Candidate-tree verifier for the sandboxed `git pr` helper.
#
# The helper intentionally denies network access, replaces HOME with private
# scratch space, excludes user-local tools from PATH, and clones no gitignored
# .venv. Those boundaries make eleven full-sweep gates impossible by construction:
#
#   zizmor, licenses,
#   clang-tidy             user-local pinned tools are outside the clean PATH
#   twin-wasm              emsdk normally lives under the real HOME
#   patch-drift            reads pinned public upstream revisions over network
#   test, coverage         require python modules from the gitignored .venv
#   test-tui               tools/tui/node_modules is gitignored, and restoring it
#                          needs both the network and bun's cache under the real
#                          HOME, so every step of the gate is unreachable
#   semgrep                the six registry packs are fetched per run
#   web                    retire.js downloads its advisory repository per run
#   deps                   pip-audit queries PyPI, osv-scanner queries OSV
#
# The last three are the security gates that ask a question about the outside
# world: "is this dependency known bad TODAY". That answer is not in the tree, so
# there is nothing a cached copy could honestly substitute — a sandbox with no
# network cannot answer it, and pretending otherwise would be worse than saying
# so. They are also the three that changed most recently, which is exactly why
# they get named here rather than left to fail as if something were wrong.
#
# CI still runs all eleven. The ordinary developer sweep remains `make verify`;
# this wrapper exists only so the isolated candidate can run every gate its
# declared capabilities actually support without weakening verify.sh's rule
# that an accidentally missing tool is fatal.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# twin-wasm normally runs this before rebuilding. Keep the committed-WASM
# behavior check even though the sandbox cannot discover emcc for the rebuild.
node "$ROOT/web-twin/selftest.cjs"

ISOLATED_SKIP="zizmor licenses clang-tidy twin-wasm patch-drift test coverage test-tui"
ISOLATED_SKIP="$ISOLATED_SKIP semgrep web deps"
export SKIP="${SKIP:+$SKIP }$ISOLATED_SKIP"
exec "$ROOT/scripts/verify.sh"
