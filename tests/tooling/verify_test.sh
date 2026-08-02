#!/usr/bin/env bash
# verify_test.sh — tests for the pre-push sweep (scripts/verify.sh).
#
# The sweep exists to make "it passed locally" and "it will pass CI" the same
# sentence. That promise breaks in two different ways, so this file tests two
# different things:
#
#   Part 1, static: the gate table still covers every CI job. A job added to
#   .github/workflows/ with no row in verify.sh is the original bug returning —
#   the sweep goes green having never run it. No amount of running the sweep
#   detects that, so it is checked against the workflow files themselves rather
#   than against a count someone remembered to update.
#
#   Part 2, behavioral: a real copy of verify.sh, run against stub tools in a
#   throwaway git repo. Every gate is a stub that passes or fails on command, so
#   a whole sweep costs about half a second and the interesting cases (a missing
#   tool, a failing tripwire, a coverage floor that is not met) can be produced
#   on demand instead of waited for. What is under test is the scheduling and
#   the reporting: which gates run, which are skipped and why, and — the one
#   that matters most — whether the exit status can ever say "fine" about a gate
#   that did not actually run.
#
# Hermetic: a temp dir, its own git repo, and a PATH containing nothing but the
# stubs plus symlinks to the handful of system tools verify.sh itself uses. That
# last part is what lets a tool be "missing" here at all, and it means the result
# does not depend on what happens to be installed on the host.
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VERIFY="$REPO/scripts/verify.sh"
ISOLATED_VERIFY="$REPO/scripts/verify-isolated.sh"
COV_MIN="${COV_MIN:-90}" # gate_label interpolates it; see the lift below

TMP="$(mktemp -d "${TMPDIR:-/tmp}/verifytest.XXXXXX")"
TMP="$(cd "$TMP" && pwd -P)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
ok()  { printf '  \033[32m✓\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  \033[31m✗\033[0m %s\n' "$1"; fail=$((fail+1)); }
# assert NAME COND-CMD... — runs the command; pass iff it exits 0. No eval.
assert() { local n="$1"; shift; if "$@"; then ok "$n"; else bad "$n"; fi; }
# has REGEX — greps the last captured $out. Case-sensitive, because the
# difference between "skipped" and "SKIPPED" is a difference in meaning here.
# Summary rows are matched with " +" between the columns rather than counted
# spaces: the column widths are formatting, not behavior.
has()  { printf '%s' "${out:-}" | grep -qE -- "$1"; }
hasnt() { ! has "$1"; }
# passed LABEL — the gate whose label is LABEL ran and passed. A duration is
# what says so: only a passing row carries one, where skipped and not-run rows
# carry a reason instead. Matched by label rather than gate name because the
# gate name also appears in the skip and failure lines.
passed() { has "$1 +[0-9]+s"; }
notpassed() { ! passed "$1"; }

# ---------------------------------------------------------------------------
# Part 1: the gate table against the workflows it claims to mirror
# ---------------------------------------------------------------------------
# verify.sh's own tables, lifted rather than run — sourcing the script would
# start a sweep. They are plain arrays and bash-3.2 case functions, which is
# what makes reading them without executing anything possible.
eval "$(awk '/^GATES=\(/,/^\)$/' "$VERIFY")"
eval "$(awk '/^gate_need\(\)/,/^}$/' "$VERIFY")"
eval "$(awk '/^gate_need_py\(\)/,/^}$/' "$VERIFY")"
eval "$(awk '/^gate_label\(\)/,/^}$/' "$VERIFY")"

echo "== gate table covers every CI job =="

# The mapping, maintained by hand because it is a judgement, not a derivation:
# a CI job either has a gate that reproduces it locally, or a reason it cannot
# have one. Adding a job to .github/workflows/ fails this list until someone
# writes down which of the two it is — which is the entire point of the file.
#
#   <workflow>:<job>  <gate>       reproduced locally by that gate
#   <workflow>:<job>  !<reason>    deliberately not reproduced
CI_MAP="
ci.yml:verify                              test-web
ci.yml:verify                              actionlint
ci.yml:verify                              zizmor
ci.yml:verify                              mal-diff
ci.yml:verify                              esp
ci.yml:verify                              attest
ci.yml:verify                              approtect
ci.yml:verify                              uwb-seam
ci.yml:verify                              ct
ci.yml:verify                              format
ci.yml:verify                              shellcheck
ci.yml:verify                              secrets
ci.yml:verify                              web
ci.yml:verify                              licenses
ci.yml:verify                              fuzz
ci.yml:verify                              clang-tidy
ci.yml:verify                              test
ci.yml:verify                              test-san
ci.yml:verify                              patch-drift
ci.yml:verify                              docs
ci.yml:verify                              deps
ci.yml:verify                              test-port
ci.yml:verify                              test-verify
ci.yml:verify                              coverage
ci.yml:verify                              semgrep
ci.yml:verify                              cbmc
docs.yml:build                             docs
docs.yml:publish                           !deploys the built site, not a check
presence-tags.yml:verify                   !tag-triggered: verifies a presence-signed tag, which needs an enrolled dongle and a phone in the room
security-deep.yml:secrets-history          !deep lane: scans all 576 commits (~18s and growing); the local secrets gate scans the tree, and the PR gate scans the branch range
security-deep.yml:semgrep-sarif            !deep lane: the same scan the semgrep gate runs, uploaded as SARIF at every severity instead of failing on ERROR
security-deep.yml:scorecard                !deep lane: queries GitHub own branch-protection and workflow settings, so it needs a token and the default branch
firmware-builds.yml:esp32-idf              !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:esp32-initiator        !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:nrf5340dk              !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:nrf5340dk-aliro-blob   !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:dwm3001cdk             !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:esp32-matter           !firmware: ESP-IDF/NCS toolchain
release.yml:guard                          !release: refuses a dispatch whose ref is not a vN.N.N tag, so it has nothing to reproduce locally
release.yml:tui                            test-tui
release.yml:nrf5340dk                      !release: firmware toolchain
release.yml:esp32-matter-lock              !release: firmware toolchain
release.yml:release                        !release: publishes a tag
"

# The inverse of a "!reason" row: that form is a CI job with no gate, this is a
# gate with no CI job. ci.yml runs the sweep with SKIP set for these two, so
# without this list they would read as orphans -- and the point of the file is
# that a gap is a written decision, never a silent hole.
LOCAL_ONLY="
test-ws     ws-seed.sh clones with cp -c (APFS clonefile) and fails loudly off APFS by design, so only a contributor local sweep on macOS can run it
twin-wasm   needs emsdk, a ~1 GB install for one gate; the committed twin.js is still checked by test-web, which does run in CI
"

# Every job id in every workflow, as "<file>:<job>". Job keys are the only
# two-space keys under `jobs:`; matrix, env and steps sit deeper, and a comment
# does not match the name pattern.
ALL_JOBS="$(awk '
	FNR == 1 { name = FILENAME; sub(/.*\//, "", name); injobs = 0 }
	/^jobs:/ { injobs = 1; next }
	injobs && /^  [a-zA-Z0-9_-]+:/ { k = $1; sub(/:$/, "", k); print name ":" k }
' "$REPO"/.github/workflows/*.yml)"

# Pure-bash lookups from here down. Both directions are cross products over
# small lists, and a fork per comparison turned this section into two seconds.
mapped_gate() { # <workflow:job> -> gate name or "!reason"; 1 if unmapped
	local k g
	while read -r k g _; do
		[ "$k" = "$1" ] && { printf '%s' "$g"; return 0; }
	done <<EOF
$CI_MAP
EOF
	return 1
}

unmapped="" njobs=0
while read -r job; do
	[ -n "$job" ] || continue
	njobs=$((njobs + 1))
	mapped_gate "$job" >/dev/null || unmapped="${unmapped:+$unmapped }$job"
done <<EOF
$ALL_JOBS
EOF
assert "every CI job is accounted for${unmapped:+ — UNMAPPED: $unmapped}" \
	test -z "$unmapped"
assert "and there are jobs to account for" test "$njobs" -gt 10

# The other direction: a gate mapped to a job that no longer exists runs
# something CI does not, which is the same drift pointing the other way.
jobs_ss="$(printf ' %s ' "$(printf '%s' "$ALL_JOBS" | tr '\n' ' ')")"
stale=""
while read -r key gate _; do
	[ -n "${key:-}" ] || continue
	case "$gate" in !*) continue ;; esac
	case "$jobs_ss" in *" $key "*) ;; *) stale="${stale:+$stale }$key" ;; esac
done <<EOF
$CI_MAP
EOF
assert "no gate maps to a job that is gone${stale:+ — STALE: $stale}" test -z "$stale"

# Both remaining directions between the map and the gate table. A gate no job
# claims is time spent on a check the PR will not make; a job whose gate has
# been deleted is a job with nothing running it, wearing a mapping that says
# otherwise.
gates_claimed=""
missing_gate=""
while read -r key gate _; do
	[ -n "${key:-}" ] || continue
	case "$gate" in !*) continue ;; esac
	gates_claimed="$gates_claimed $gate"
	case " ${GATES[*]} " in
	*" $gate "*) ;;
	*) missing_gate="${missing_gate:+$missing_gate }$key->$gate" ;;
	esac
done <<EOF
$CI_MAP
EOF
local_only=""
while read -r g _; do
	[ -n "${g:-}" ] && local_only="$local_only $g"
done <<EOF
$LOCAL_ONLY
EOF
orphan=""
for g in "${GATES[@]}"; do
	case " $gates_claimed " in *" $g "*) continue ;; esac
	case " $local_only " in *" $g "*) continue ;; esac
	orphan="${orphan:+$orphan }$g"
done
assert "every gate maps to a CI job or is declared local-only${orphan:+ — ORPHANS: $orphan}" \
	test -z "$orphan"

# A local-only entry for a gate that CI does run is a stale excuse, and it would
# hide a real orphan the day the gate leaves the sweep.
stale_local=""
for g in $local_only; do
	case " $gates_claimed " in *" $g "*) stale_local="${stale_local:+$stale_local }$g" ;; esac
done
assert "no local-only gate is also claimed by a CI job${stale_local:+ — $stale_local}" \
	test -z "$stale_local"
assert "every mapped job still has its gate${missing_gate:+ — GONE: $missing_gate}" \
	test -z "$missing_gate"

# Advice has to be true: verify.sh tells you to run `make tools-install` when a
# gate's tool is missing, so every tool a gate names must be one that installs.
eval "$(awk '/^TOOLS=\(/,/^\)$/' "$REPO/scripts/toolchain.sh")"
uninstallable=""
for g in "${GATES[@]}"; do
	for t in $(gate_need "$g") $(gate_need_py "$g"); do
		case " ${TOOLS[*]} " in
		*" $t "*) ;;
		*) uninstallable="${uninstallable:+$uninstallable }$g:$t" ;;
		esac
	done
done
assert "every tool a gate needs is installable${uninstallable:+ — $uninstallable}" \
	test -z "$uninstallable"

# A row with no label prints a blank column in the summary instead of saying
# what failed, which is a bad moment to discover it.
unlabelled=""
for g in "${GATES[@]}"; do
	[ -n "$(gate_label "$g")" ] || unlabelled="${unlabelled:+$unlabelled }$g"
done
assert "every gate has a summary label${unlabelled:+ — $unlabelled}" test -z "$unlabelled"

# ---------------------------------------------------------------------------
# Part 2: the sweep's behavior, against stubs
# ---------------------------------------------------------------------------
BIN="$TMP/bin"; FAKE="$TMP/repo"
mkdir -p "$BIN" "$TMP/home"

# PATH is these symlinks and nothing else, so "installed" becomes a property of
# this directory rather than of the host. verify.sh and the stubs use: bash
# (their own shebang), git, python3 (the coverage floor and the licence filter,
# both run for real), and the coreutils the script itself calls.
NEED_REAL="bash git python3 date mktemp mv rm cat xargs uname sed grep sort head tail wc dirname mkdir chmod ln cp touch env"
[ "$(uname -s)" = Darwin ] && NEED_REAL="$NEED_REAL xcrun"
missing_real=""
for t in $NEED_REAL; do
	p="$(command -v "$t" 2>/dev/null)" && [ -x "$p" ] \
		&& ln -sf "$p" "$BIN/$t" || missing_real="${missing_real:+$missing_real }$t"
done
if [ -n "$missing_real" ]; then
	printf '  \033[31m✗\033[0m cannot build the sandbox PATH, missing: %s\n' "$missing_real"
	exit 1
fi

# One stub per gate tool, each knowing the gate it belongs to and failing when
# that gate is named in $FAIL_GATES. That is how a failing gate is produced
# without anything real being run.
mk_tool_stub() { # <tool> <gate>
	cat > "$BIN/$1" <<EOF
#!/usr/bin/env bash
case " \${FAIL_GATES:-} " in *" $2 "*) echo "stub $1: $2 failed" >&2; exit 1 ;; esac
echo "stub $1 ok"
EOF
	chmod +x "$BIN/$1"
}
mk_tool_stub clang-format format
mk_tool_stub shellcheck shellcheck
mk_tool_stub actionlint actionlint
mk_tool_stub zizmor zizmor
mk_tool_stub clang-tidy clang-tidy
mk_tool_stub node twin-wasm
mk_tool_stub doxygen docs
mk_tool_stub dot docs
mk_tool_stub cbmc cbmc
# test-tui is the one gate that shells out to a tool directly instead of through
# `make`, so it needs both a stub and somewhere to cd into below.
mk_tool_stub bun test-tui
# The four security gates. These stubs exist only so the missing-tool check sees
# them as present — the gates themselves go through the scripts/security.sh stub
# below, which is what decides pass or fail.
mk_tool_stub gitleaks secrets
mk_tool_stub retire web
mk_tool_stub semgrep semgrep
mk_tool_stub osv-scanner deps
mk_tool_stub pip-audit deps

# `make <target>`: for every gate that shells out to make, the gate name and the
# target are the same word, so one stub covers all of them. It also writes the
# coverage summary the floor check reads, at whatever percentage is asked for.
cat > "$BIN/make" <<'EOF'
#!/usr/bin/env bash
t=""
for a in "$@"; do case "$a" in -*) ;; *) t="$a"; break ;; esac; done
case " ${FAIL_GATES:-} " in *" $t "*) echo "stub make: $t failed" >&2; exit 1 ;; esac
case " ${EXIT2_GATES:-} " in *" $t "*) echo "stub make: $t exited 2" >&2; exit 2 ;; esac
if [ "$t" = coverage ]; then
	# Same path tests/host/coverage.sh writes: one build root, host suites
	# under build/host. A stale path here makes the floor check read a file
	# that is never written, which fails as "no coverage summary".
	mkdir -p build/host/coverage
	printf '{"data":[{"totals":{"lines":{"percent":%s}}}]}\n' "${COV_PCT:-95.5}" \
		> build/host/coverage/summary.json
fi
[ "$t" = twin-wasm ] && [ -n "${TWIN_DRIFTS:-}" ] && echo "rebuilt" >> web-twin/twin.js
echo "stub make $t"
EOF
chmod +x "$BIN/make"

# `reuse lint --json`. The default output is non-compliant in the two categories
# CI reports but does not gate on, so a pass here tests the filter rather than an
# empty document. REUSE_BAD=1 adds a category that is gated.
cat > "$BIN/reuse" <<'EOF'
#!/usr/bin/env bash
gated='"unused_licenses": ["LicenseRef-nobody-claims-me"],'
printf '{"non_compliant": {%s "missing_copyright_info": ["a.c", "b.c"], "missing_licensing_info": ["a.c"]}}\n' \
	"${REUSE_BAD:+$gated}"
EOF
chmod +x "$BIN/reuse"

# Stands in for $PY in the python-module check. verify.sh only ever calls it as
# `$PY -c "import <mod>"`, so PY_MISSING="markdown" makes a module absent
# without uninstalling anything.
cat > "$BIN/pystub" <<'EOF'
#!/usr/bin/env bash
mod="${2#import }"
case " ${PY_MISSING:-} " in *" $mod "*) exit 1 ;; esac
EOF
chmod +x "$BIN/pystub"

# The repo the sweep runs in: enough tracked files for the gates that shell out
# to `git ls-files`, and the two paths verify.sh calls directly.
mkdir -p "$FAKE/scripts" "$FAKE/tests/host" "$FAKE/tests/tooling" \
	"$FAKE/modules" "$FAKE/web-twin" "$FAKE/tools/tui"
cp "$VERIFY" "$FAKE/scripts/verify.sh"; chmod +x "$FAKE/scripts/verify.sh"
cp "$ISOLATED_VERIFY" "$FAKE/scripts/verify-isolated.sh"
chmod +x "$FAKE/scripts/verify-isolated.sh"
cat > "$FAKE/tests/host/sources.sh" <<'EOF'
# stub of the real sources.sh: verify.sh wants $PY and three arrays from it.
PY="pystub"
UNIT_SRCS=(modules/a.c)
DEFS=(-DSTUB=1)
INCS=(-Imodules)
EOF
cat > "$FAKE/tests/tooling/patch_drift_check.sh" <<'EOF'
#!/usr/bin/env bash
case " ${FAIL_GATES:-} " in *" patch-drift "*) echo "stub: drifted" >&2; exit 1 ;; esac
echo "stub patch-drift ok"
EOF
chmod +x "$FAKE/tests/tooling/patch_drift_check.sh"
# All four security gates dispatch through this one script, so it stands in for
# all four. It takes the gate name as $1, which is exactly how verify.sh calls
# the real one — meaning FAIL_GATES can fail any of them individually.
cat > "$FAKE/scripts/security.sh" <<'EOF'
#!/usr/bin/env bash
case " ${FAIL_GATES:-} " in *" $1 "*) echo "stub security: $1 failed" >&2; exit 1 ;; esac
case " ${HOSTSKIP_GATES:-} " in *" $1 "*) echo "stub security: $1 needs a tool this host cannot have" >&2; exit 2 ;; esac
echo "stub security $1 ok"
EOF
chmod +x "$FAKE/scripts/security.sh"
# The approtect gate is a tripwire, so without a stub here it fails first and
# every later assertion in this file reads as a cascade from it rather than as
# its own result. verify.sh invokes it twice — `--self-test`, then bare — and
# both have to succeed for the gate to pass, so the stub answers both.
cat > "$FAKE/scripts/check-approtect.sh" <<'EOF'
#!/usr/bin/env bash
case " ${FAIL_GATES:-} " in *" approtect "*) echo "stub approtect: failed" >&2; exit 1 ;; esac
case "${1:-}" in --self-test) echo "stub approtect self-test ok" ;; *) echo "stub approtect ok" ;; esac
EOF
chmod +x "$FAKE/scripts/check-approtect.sh"
# Same story for the uwb-seam gate: also a tripwire, also invoked twice
# (--self-test, then bare), so it needs the same two-answer stub or its failure
# cascades over every assertion below.
cat > "$FAKE/scripts/check-uwb-seam.sh" <<'EOF'
#!/usr/bin/env bash
case " ${FAIL_GATES:-} " in *" uwb-seam "*) echo "stub uwb-seam: failed" >&2; exit 1 ;; esac
case "${1:-}" in --self-test) echo "stub uwb-seam self-test ok" ;; *) echo "stub uwb-seam ok" ;; esac
EOF
chmod +x "$FAKE/scripts/check-uwb-seam.sh"
echo "int a;" > "$FAKE/modules/a.c"
echo "// twin" > "$FAKE/web-twin/twin.js"
echo "// selftest" > "$FAKE/web-twin/selftest.cjs"
printf '/build/\n' > "$FAKE/.gitignore"
git -C "$FAKE" init -q
git -C "$FAKE" config user.email t@t; git -C "$FAKE" config user.name t
git -C "$FAKE" add -A >/dev/null 2>&1
git -C "$FAKE" commit -qm init >/dev/null 2>&1

NGATES=${#GATES[@]}

# runv ENV=VAL... — one full sweep in the sandbox, into $out and $rc. It puts
# the tree back afterwards so no scenario can leak into the next one.
runv() {
	out="$( cd "$FAKE" && env -i PATH="$BIN" HOME="$TMP/home" TMPDIR="$TMP" \
		NO_COLOR=1 "$@" ./scripts/verify.sh 2>&1 )"
	rc=$?
	git -C "$FAKE" checkout -q -- . 2>/dev/null
	rm -rf "$FAKE/build"
	return 0
}

echo
echo "== the sweep, against stub tools =="

# S1: everything green. The counts come from the lifted gate table, not from a
# number typed here, so adding a gate cannot make this quietly wrong. This one
# run also carries the default-state assertions in S2 and S3.
runv
assert "S1 a clean sweep exits 0"            test "$rc" -eq 0
assert "S1 every gate but one passed"        has "$((NGATES - 1)) passed"
assert "S1 no gate reports FAILED"           hasnt "FAILED"
# One report, not two: each gate prints its own row as it finishes and nothing
# reprints them afterwards, so a gate appears exactly once. The label is what
# proves the row is the live one rather than a bare name in the verdict.
assert "S1 rows print live, with labels"     has "twin constants match the firmware"
assert "S1 and each gate appears once"       test "$(printf '%s' "$out" | grep -cE 'twin constants match the firmware')" -eq 1
assert "S1 and the parallel phase announces" has "lanes in parallel"

# S2: cbmc is opt-in, and still gets a row saying so. A gate that vanishes from
# the summary is indistinguishable from one that passed.
assert "S2 cbmc is held back by default"     has "cbmc .*skipped"
assert "S2 and the row explains why"         has "opt-in, WITH_CBMC=1"

# S3: the licence gate reproduces what CI gates on, which is the licence store,
# not full REUSE compliance — header-coverage categories are reported, not
# enforced, and the stub returns two of them on every run.
assert "S3 header notes do not fail it"      passed "licence store is consistent"

# S4: WITH_CBMC=1 puts it back, and then nothing is held back at all.
runv WITH_CBMC=1
assert "S4 WITH_CBMC=1 runs cbmc"            passed "wire-parser memory-safety proof"
assert "S4 and then every gate ran"          has "all $NGATES host-runnable CI gates passed"

# S5: gates in two different lanes fail. The run must fail, name both — showing
# only the first sends someone round the fix-and-push loop twice — print their
# output, and still report the lanes that were running alongside.
runv FAIL_GATES="docs fuzz"
assert "S5 a failing gate exits 1"           test "$rc" -eq 1
assert "S5 both failures are named"          has "verify FAILED: fuzz docs"
assert "S5 prints the gate's output"         has "stub make: docs failed"
assert "S5 and points at the full log"       has "full log: +.*/docs\.out"
assert "S5 other lanes still ran"            passed "line coverage >= [0-9]+%"
assert "S5 its lane-mate is 'not run'"       has "clang-tidy .*lane stopped"
assert "S5 and says which gate stopped it"   has "its lane stopped at docs"

# S6: the tripwire. A one-second formatting slip stops the sweep before the
# expensive phase starts, and everything after it reads as not-run, not passed.
runv FAIL_GATES="format"
assert "S6 tripwire failure exits 1"         test "$rc" -eq 1
assert "S6 later gates did not run"          has "tripwire failed at format"
assert "S6 nothing after it claims to pass"  notpassed "line coverage >= [0-9]+%"

# S7: THE one that matters. A gate whose tool is absent must fail the sweep.
# Exiting 0 here is the whole bug this sweep was written to remove: the push
# lands, and CI runs the gate that never ran locally.
mv "$BIN/zizmor" "$TMP/zizmor.hidden"
runv
mv "$TMP/zizmor.hidden" "$BIN/zizmor"
assert "S7 a missing tool FAILS the sweep"   test "$rc" -eq 1
assert "S7 says the tool is the reason"      has "needs zizmor"
assert "S7 says it on the live row too"      has "CI still runs it"
assert "S7 counts it apart from a skip"      has "COULD NOT RUN"
assert "S7 points at the fix"                has "make tools-install"

# S8: the same, for a python module — invisible to `command -v`, which is how
# these two went unnoticed in the first place.
runv PY_MISSING="markdown"
assert "S8 a missing python module fails"    test "$rc" -eq 1
assert "S8 names the module, not a binary"   has "needs python:markdown"

# S9: SKIP= is a decision rather than an accident, so it passes — but loudly,
# and the gate really does not run.
runv SKIP="docs fuzz"
assert "S9 SKIP= still exits 0"              test "$rc" -eq 0
assert "S9 warns it was not the full set"    has "NOT the full CI set"
assert "S9 lists what CI will still run"     has "docs \(SKIP=\)"
assert "S9 the skipped gate did not run"     hasnt "stub make docs"

# S10: coverage is `make coverage` PLUS the floor CI enforces as a separate
# step. Running the target alone would pass here and fail there.
runv COV_PCT=95.0 COV_MIN=96
assert "S10 below the floor fails"           test "$rc" -eq 1
assert "S10 names coverage"                  has "verify FAILED: coverage"
runv COV_PCT=90.0
assert "S10 exactly at the floor passes"     test "$rc" -eq 0

# S11: a broken licence store does fail it, so S3's pass was the filter working
# and not the check being absent.
runv REUSE_BAD=1
assert "S11 a broken licence store fails"    test "$rc" -eq 1
assert "S11 and names the category"          has "unused_licenses"

# S12: the twin rebuild diff is warn-only in CI (emsdk output differs by host
# OS), so it must be warn-only here too, or the sweep is stricter than the PR
# and stops a push CI would have accepted. The note itself lands in the gate's
# captured output, which a passing sweep does not print.
runv TWIN_DRIFTS=1
assert "S12 a twin.js diff is warn-only"     test "$rc" -eq 0
assert "S12 twin-wasm still passes"          passed "node selftest"

# S13: SERIAL=1 is the same sweep, one gate at a time. Same verdict, or it is
# not a debugging aid but a second, differently-behaved gate set.
runv SERIAL=1
assert "S13 serial clean sweep exits 0"      test "$rc" -eq 0
assert "S13 serial reports no lanes"         hasnt "lanes in parallel"
runv SERIAL=1 FAIL_GATES="test-port"
assert "S13 serial fails the same way"       test "$rc" -eq 1
assert "S13 and names the same gate"         has "verify FAILED: test-port"

# S14: the script's own lane-coverage check. A gate added to the table but to no
# lane keeps its summary row and never runs — the exact failure this whole file
# guards against, so the guard is tested rather than trusted.
mutant() { # <sed expression> -> $out, $rc
	sed "$1" "$VERIFY" > "$FAKE/scripts/mutant.sh"
	chmod +x "$FAKE/scripts/mutant.sh"
	out="$( cd "$FAKE" && env -i PATH="$BIN" HOME="$TMP/home" TMPDIR="$TMP" \
		NO_COLOR=1 ./scripts/mutant.sh 2>&1 )"
	rc=$?
	rm -f "$FAKE/scripts/mutant.sh"
	return 0
}
mutant 's/^GATES=($/GATES=(\n\tstrandedgate/'
assert "S14 a gate in no lane is refused"    test "$rc" -eq 2
assert "S14 and it is named"                 has "strandedgate\(in 0 lanes\)"
mutant 's/^\t"coverage"  /\t"coverage stowaway"  /'
assert "S14 a lane entry that is no gate is refused" test "$rc" -eq 2
assert "S14 and that is named too"           has "stowaway\(not a gate\)"

# S15: result storage is a prerequisite, not an optional convenience. The
# verifier must stop before running a gate when its temp directory cannot be
# created, rather than falling through with an empty RUNDIR and writing at /.
mv "$BIN/mktemp" "$TMP/mktemp.real"
cat >"$BIN/mktemp" <<'EOF'
#!/usr/bin/env bash
exit 73
EOF
chmod +x "$BIN/mktemp"
runv
rm -f "$BIN/mktemp"
mv "$TMP/mktemp.real" "$BIN/mktemp"
assert "S15 result-directory failure exits nonzero" test "$rc" -ne 0
assert "S15 explains that verification could not start" \
	has "could not create the result directory"
assert "S15 never claims zero gates passed" hasnt "all 0 host-runnable CI gates passed"

# S16: defend independently against losing a per-gate result after RUNDIR was
# created. A lane can print a failure that the parent never sees if the atomic
# rename fails; an incomplete result set must therefore be fatal on its own.
mv "$BIN/mv" "$TMP/mv.real"
cat >"$BIN/mv" <<'EOF'
#!/usr/bin/env bash
exit 74
EOF
chmod +x "$BIN/mv"
runv
rm -f "$BIN/mv"
mv "$TMP/mv.real" "$BIN/mv"
assert "S16 lost gate result exits nonzero" test "$rc" -ne 0
assert "S16 names incomplete bookkeeping" has "result bookkeeping incomplete"
assert "S16 never claims zero gates passed" hasnt "all 0 host-runnable CI gates passed"

# S17: the tracked git-pr wrapper scopes out only capabilities its sandbox
# deliberately removes, still runs the committed twin self-test, and makes the
# reduced scope loud rather than pretending it reproduced the full CI set.
out="$(cd "$FAKE" && env -i PATH="$BIN" HOME="$TMP/home" TMPDIR="$TMP" \
	NO_COLOR=1 ./scripts/verify-isolated.sh 2>&1)"
rc=$?
assert "S17 isolated candidate sweep exits 0" test "$rc" -eq 0
assert "S17 committed twin selftest still runs" has "stub node ok"
assert "S17 reduced scope is explicit" has "NOT the full CI set"
for isolated_gate in zizmor licenses clang-tidy twin-wasm patch-drift test coverage test-tui \
	semgrep web deps; do
	assert "S17 skips unavailable $isolated_gate gate" \
		has "$isolated_gate \\(SKIP=\\)"
done
assert "S17 still runs hermetic sanitizer gate" passed "host suite under ASan \\+ UBSan"

# S18: a gate that exits 2 could not answer the question on this host. `ct` does
# it on every macOS run, because there is no valgrind for darwin/arm64 and no
# install fixes that. It must not be a pass — a green row for a gate that ran
# nothing is precisely how a clean local sweep meets a red CI — and it must not
# fail the sweep either, because a permanent red on the primary dev machine is a
# red everyone learns to ignore. Loud, counted as skipped, exit 0.
runv HOSTSKIP_GATES="ct"
assert "S18 exit 2 is not a pass"            notpassed "no secret-dependent branches"
assert "S18 and says so in the row"          has "NOT CHECKED"
assert "S18 and is counted as skipped"       has "gate\\(s\\) SKIPPED"
assert "S18 and the verdict is not all-pass" hasnt "all $NGATES host-runnable CI gates passed"
assert "S18 and the sweep still exits 0"     test "$rc" -eq 0
assert "S18 and it is not reported failed"   hasnt "verify FAILED"

# S19: and only for that family. Every other gate owns its own exit codes, and
# docs already uses 2 for "this branch is behind origin/main" — a real problem
# with an obvious fix. Reading that as "the host cannot check" would turn a
# blocking answer into a footnote, which is the first version of S18 verbatim.
runv EXIT2_GATES="docs"
assert "S19 exit 2 elsewhere is still a failure" has "docs .*FAILED \\(exit 2\\)"
assert "S19 and it fails the sweep"              test "$rc" -ne 0
assert "S19 and it is not called NOT CHECKED"    hasnt "NOT CHECKED"

echo
if [ "$fail" -eq 0 ]; then
	printf '\033[32mall %d checks passed\033[0m\n' "$pass"; exit 0
else
	printf '\033[31m%d/%d checks failed\033[0m\n' "$fail" "$((pass+fail))"; exit 1
fi
