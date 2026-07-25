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
cbmc.yml:cbmc                              cbmc
clang-tidy.yml:clang-tidy                  clang-tidy
docs.yml:build                             docs
docs.yml:publish                           !deploys the built site, not a check
format.yml:clang-format                    format
fuzz.yml:libfuzzer                         fuzz
host-tests.yml:test                        test
host-tests.yml:portability                 !the same 'make test' on a 2nd OS/compiler
host-tests.yml:coverage                    coverage
patch-drift.yml:drift                      patch-drift
port-tests.yml:test                        test-port
sanitizers.yml:asan-ubsan                  test-san
tooling.yml:ws-seed                        test-ws
tooling.yml:verify-tests                   test-verify
tooling.yml:shellcheck                     shellcheck
tooling.yml:licenses                       licenses
twin-web.yml:drift-gate                    test-web
twin-web.yml:wasm-firmware                 twin-wasm
workflow-lint.yml:actionlint               actionlint
workflow-lint.yml:zizmor                   zizmor
firmware-builds.yml:changes                !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:esp32-idf              !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:nrf5340dk              !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:nrf5340dk-aliro-source !firmware: ESP-IDF/NCS toolchain
firmware-builds.yml:esp32-matter           !firmware: ESP-IDF/NCS toolchain
release.yml:nrf5340dk                      !release: firmware toolchain
release.yml:esp32-matter-lock              !release: firmware toolchain
release.yml:release                        !release: publishes a tag
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
orphan=""
for g in "${GATES[@]}"; do
	case " $gates_claimed " in *" $g "*) ;; *) orphan="${orphan:+$orphan }$g" ;; esac
done
assert "every gate maps to a CI job${orphan:+ — ORPHANS: $orphan}" test -z "$orphan"
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
NEED_REAL="bash git python3 date mktemp mv rm cat xargs uname sed grep sort head dirname mkdir chmod ln cp touch env"
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

# `make <target>`: for every gate that shells out to make, the gate name and the
# target are the same word, so one stub covers all of them. It also writes the
# coverage summary the floor check reads, at whatever percentage is asked for.
cat > "$BIN/make" <<'EOF'
#!/usr/bin/env bash
t=""
for a in "$@"; do case "$a" in -*) ;; *) t="$a"; break ;; esac; done
case " ${FAIL_GATES:-} " in *" $t "*) echo "stub make: $t failed" >&2; exit 1 ;; esac
if [ "$t" = coverage ]; then
	mkdir -p build/coverage
	printf '{"data":[{"totals":{"lines":{"percent":%s}}}]}\n' "${COV_PCT:-95.5}" \
		> build/coverage/summary.json
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
	"$FAKE/modules" "$FAKE/web-twin"
cp "$VERIFY" "$FAKE/scripts/verify.sh"; chmod +x "$FAKE/scripts/verify.sh"
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
assert "S1 every gate but one passed"        has "$((NGATES - 1)) gates passed"
assert "S1 no gate reports FAILED"           hasnt "FAILED"
# Two reports, not one: rows printed live as each gate finishes, and a summary
# rebuilt afterwards from the result files. Only the live rows carry the gate
# labels, so a label is what tells the two apart from out here.
assert "S1 rows print live, with labels"     has "twin constants match the firmware"
assert "S1 and the parallel phase announces" has "lanes in parallel"

# S2: cbmc is opt-in, and still gets a row saying so. A gate that vanishes from
# the summary is indistinguishable from one that passed.
assert "S2 cbmc is held back by default"     has "cbmc +skipped"
assert "S2 and the row explains why"         has "opt-in, WITH_CBMC=1"

# S3: the licence gate reproduces what CI gates on, which is the licence store,
# not full REUSE compliance — header-coverage categories are reported, not
# enforced, and the stub returns two of them on every run.
assert "S3 header notes do not fail it"      has "licenses +passed"

# S4: WITH_CBMC=1 puts it back, and then nothing is held back at all.
runv WITH_CBMC=1
assert "S4 WITH_CBMC=1 runs cbmc"            has "cbmc +passed"
assert "S4 and then every gate ran"          has "all $NGATES host-runnable CI gates passed"

# S5: gates in two different lanes fail. The run must fail, name both — showing
# only the first sends someone round the fix-and-push loop twice — print their
# output, and still report the lanes that were running alongside.
runv FAIL_GATES="docs fuzz"
assert "S5 a failing gate exits 1"           test "$rc" -eq 1
assert "S5 both failures are named"          has "verify FAILED: fuzz docs"
assert "S5 prints the gate's output"         has "stub make: docs failed"
assert "S5 other lanes still ran"            has "coverage +passed"
assert "S5 its lane-mate is 'not run'"       has "clang-tidy +not run"
assert "S5 and says which gate stopped it"   has "its lane stopped at docs"

# S6: the tripwire. A one-second formatting slip stops the sweep before the
# expensive phase starts, and everything after it reads as not-run, not passed.
runv FAIL_GATES="format"
assert "S6 tripwire failure exits 1"         test "$rc" -eq 1
assert "S6 later gates did not run"          has "tripwire failed at format"
assert "S6 nothing after it claims to pass"  hasnt "coverage +passed"

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
assert "S12 twin-wasm still passes"          has "twin-wasm +passed"

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

echo
if [ "$fail" -eq 0 ]; then
	printf '\033[32mall %d checks passed\033[0m\n' "$pass"; exit 0
else
	printf '\033[31m%d/%d checks failed\033[0m\n' "$fail" "$((pass+fail))"; exit 1
fi
