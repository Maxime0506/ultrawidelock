#!/usr/bin/env bash
#
# security.sh — the four fast security gates, in one place.
#
# CI (.github/workflows/security.yml), `make security` and the `secrets`/`mal-diff`/`semgrep`/
# `deps` rows in scripts/verify.sh all call THIS file. That is the point of it: the repo already
# learned once that a gate reproduced by hand in two places drifts in one of them, which is why
# verify.sh's header insists on running the same command CI runs. Here there is only one command.
#
#   scripts/security.sh              # all four gates, in order
#   scripts/security.sh semgrep      # one gate
#   make security                    # same thing, through the front door
#
# Gates:
#   secrets    gitleaks over the tree, or over a commit range when one is given
#   mal-diff   scripts/security-diff.sh, the structural malicious-change checks
#   semgrep    security/*.yml plus the pinned registry packs; ERROR blocks, WARNING reports
#   deps       osv-scanner on the bun lockfile, pip-audit on the Home Assistant dependencies
#
# Exit 0 if every gate selected passed, 1 otherwise. A gate whose tool is missing FAILS rather
# than skipping, for the reason verify.sh gives at length: CI runs it whatever this host has, so
# "could not check" has to read as "not verified", never as "fine".
#
# Env:
#   SECURITY_BASE / SECURITY_HEAD   commit range; CI passes the PR's base and head
#   SEMGREP_NO_REGISTRY=1           local rulesets only, skipping the network fetch
#   NO_COLOR=1                      plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' GRN=$'\033[32m'
	YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗"
else
	BOLD="" DIM="" RED="" GRN="" YEL="" RESET=""
	CHK="+" CRS="x"
fi

BASE="${SECURITY_BASE:-}"
HEAD_REF="${SECURITY_HEAD:-HEAD}"

have() { command -v "$1" >/dev/null 2>&1; }

hdr() { printf '\n%s── %s %s%s\n' "$BOLD" "$1" "${DIM}" "$RESET"; }
missing() {
	printf '  %s%s%s %s is not installed — %s\n' "$RED" "$CRS" "$RESET" "$1" "$2"
	printf '      A gate that cannot run is a gate that did not pass. CI runs it regardless.\n'
	return 1
}

# ---- secrets ---------------------------------------------------------------
# Two scopes on purpose. With a range, only the commits being proposed are scanned, which is what
# a pull request needs and costs about two seconds. Without one, the whole working tree is
# scanned. Neither is the full-history scan — that lives in the weekly deep lane, because at ~18s
# over 576 commits it is too slow to sit in front of every push and its answer changes only when
# history is rewritten.
gate_secrets() {
	hdr "secrets · gitleaks"
	have gitleaks || {
		missing gitleaks "brew install gitleaks"
		return 1
	}

	local args=(--redact --no-banner --config security/gitleaks.toml)
	local nrules
	# The scope line matters as much as the result. "no leaks found" over nothing looks
	# identical to "no leaks found" over the whole tree, and only one of those is good news.
	nrules="$(grep -c '^\[\[rules\]\]' security/gitleaks.toml 2>/dev/null || echo 0)"
	if [ -n "$BASE" ]; then
		printf '  %sscope: commits %s..%s  ·  gitleaks defaults + %s repo rules%s\n' \
			"$DIM" "${BASE:0:7}" "${HEAD_REF:0:7}" "$nrules" "$RESET"
		gitleaks detect --source . --log-opts="$BASE..$HEAD_REF" "${args[@]}"
	else
		printf '  %sscope: %s tracked files  ·  gitleaks defaults + %s repo rules%s\n' \
			"$DIM" "$(git ls-files | wc -l | tr -d ' ')" "$nrules" "$RESET"
		gitleaks detect --source . --no-git "${args[@]}"
	fi
}

# ---- mal-diff --------------------------------------------------------------
gate_maldiff() {
	hdr "mal-diff · structural change review"
	if [ -n "$BASE" ]; then
		scripts/security-diff.sh "$BASE" "$HEAD_REF"
	else
		scripts/security-diff.sh
	fi
}

# ---- semgrep ---------------------------------------------------------------
# One invocation with every config, not one per ruleset: semgrep parses each target file once and
# runs all loaded rules against it, so three configs in one run cost far less than three runs.
#
# The severity split is the whole contract with contributors. ERROR fails; WARNING is printed and
# does not. That is not timidity — three of this repo's WARNING rules (memcpy-from-a-wire-length,
# memset-on-a-key-buffer, all-zero-IV) are documented NOISY in security/semgrep-openaliro.yml
# because their false negatives are expensive enough to be worth their false positives. Blocking
# on them would train everyone to bypass the gate, taking the ERROR rules with it.
gate_semgrep() {
	hdr "semgrep · SAST"
	have semgrep || {
		missing semgrep "brew install semgrep, or pipx install semgrep"
		return 1
	}

	local cfg=(--config security/semgrep-openaliro.yml --config security/semgrep-malicious.yml)
	if [ -z "${SEMGREP_NO_REGISTRY:-}" ]; then
		cfg+=(--config p/c --config p/secrets --config p/python
			--config p/typescript --config p/javascript --config p/github-actions)
	else
		printf '  %sregistry packs skipped (SEMGREP_NO_REGISTRY)%s\n' "$YEL" "$RESET"
	fi

	local out rc src
	out="$(mktemp -t oa-semgrep.XXXXXX)"
	semgrep scan --metrics=off --quiet --json --output "$out" \
		"${cfg[@]}" \
		--exclude workspace --exclude build --exclude site --exclude .venv \
		--exclude node_modules --exclude deps/dw3000 --exclude docs/architecture \
		--exclude 'tests/host/fuzz/corpus' . >"$out.log" 2>&1
	src=$?
	# semgrep can fail before it writes anything: the registry packs above are fetched per run,
	# so no network means no rules, no JSON, and a python traceback where a diagnosis belongs.
	# Report what actually happened. Not downgraded to a skip — the gate genuinely did not run,
	# and on a host that is merely offline that has to read as "not verified".
	if [ ! -s "$out" ]; then
		printf '  %s%s%s semgrep produced no output (exit %s)\n' "$RED" "$CRS" "$RESET" "$src"
		printf '      The six registry packs are fetched per run, so this is what no network\n'
		printf '      looks like. SEMGREP_NO_REGISTRY=1 runs the two local rulesets only.\n'
		sed 's/^/      /' <"$out.log" | tail -n 6
		rm -f "$out" "$out.log"
		return 1
	fi
	rm -f "$out.log"
	# One --config per ruleset, so half the array length is the ruleset count.
	SEMGREP_JSON="$out" NCONFIG="$((${#cfg[@]} / 2))" python3 -c '
import json, os, sys
from collections import Counter

d = json.load(open(os.environ["SEMGREP_JSON"]))
res = d.get("results", [])
scanned = len(d.get("paths", {}).get("scanned", []))
print("  scope: %d files scanned, %d rulesets" % (scanned, int(os.environ["NCONFIG"])))
errs = [r for r in res if r["extra"]["severity"] == "ERROR"]
warns = [r for r in res if r["extra"]["severity"] != "ERROR"]

def rule(r):
    return r["check_id"].split(".")[-1]

for r in errs:
    where = "%s:%s" % (r["path"], r["start"]["line"])
    msg = r["extra"]["message"].strip().splitlines()[0][:110]
    print("  BLOCK %s  %s" % (where, rule(r)))
    print("        %s" % msg)
if warns:
    c = Counter(rule(r) for r in warns)
    tally = ", ".join("%s x%d" % (k, v) for k, v in c.most_common(6))
    print("  %d advisory finding(s): %s" % (len(warns), tally))

# A file semgrep could not parse was NOT scanned. It reports that as an error and still exits 0,
# so without this the gate is silently blind to exactly the files most likely to be doing
# something unusual. Two are known-unparseable today (Zephyr macro forms in woz_logfmt.c and
# aliro_shell.c); the count is reported every run so a third cannot appear unnoticed.
#
# Only PartialParsing counts. The "type" field on an error entry is a variant name, sometimes
# wrapped in a list with its payload, and the other variants (Timeout, OutOfMemory) say how
# loaded the machine was, not about the file. Blocking on those makes the gate flaky, and a
# blocking gate that goes green on a re-run is worse than no gate: it teaches everyone to re-run.
def etype(e):
    t = e.get("type")
    return t[0] if isinstance(t, list) else t

errored = [e for e in d.get("errors", []) if e.get("path")]
bad = sorted({e["path"] for e in errored if etype(e) == "PartialParsing"})
transient = sorted({(str(etype(e)), e["path"]) for e in errored if etype(e) != "PartialParsing"})

known = set()
try:
    with open("security/semgrep-parse-baseline.txt") as fh:
        known = {ln.strip() for ln in fh if ln.strip() and not ln.startswith("#")}
except FileNotFoundError:
    pass

new_bad = [p for p in bad if p not in known]
if bad:
    print("  note: %d file(s) unparseable, so not scanned (%d baselined)"
          % (len(bad), len(bad) - len(new_bad)))
for p in new_bad:
    print("  BLOCK %s" % p)
    print("        semgrep cannot parse this file, so NONE of its rules ran against it. That is")
    print("        a silent coverage loss, not a clean result. Fix the construct, or add the")
    print("        path to security/semgrep-parse-baseline.txt with a reason.")
for t, p in transient:
    print("  warn  %s: %s was not scanned this run" % (t, p))
    print("        A run-time limit, not a property of the file. It is a real coverage gap for")
    print("        this run only; if it repeats on an idle machine, treat it as unparseable.")

print("  %d finding(s): %d blocking, %d advisory" % (len(res), len(errs), len(warns)))
sys.exit(1 if (errs or new_bad) else 0)'
	rc=$?
	# Kept only on failure, and only then. The first time this gate blocked, it named a file and
	# deleted the evidence, so the reason could not be recovered afterwards.
	if [ "$rc" = 0 ]; then
		rm -f "$out"
	else
		printf '  %sraw semgrep output kept for diagnosis: %s%s\n' "$YEL" "$out" "$RESET"
	fi
	return "$rc"
}

# ---- deps ------------------------------------------------------------------
# osv-scanner is pointed at the lockfile rather than told to walk the tree. The walk resolves its
# root oddly under a sandboxed shell and silently reports "no package sources found" — a clean
# pass that scanned nothing. Naming the file cannot fail that way.
#
# osv-scanner is also the malicious-package half of this gate: OSV carries the OpenSSF Malicious
# Packages feed as MAL- advisories, so a dependency that is not merely vulnerable but hostile
# comes back from the same query.
gate_deps() {
	hdr "deps · known-vulnerable and known-malicious packages"
	local rc=0

	if have osv-scanner; then
		if [ -f tools/tui/bun.lock ]; then
			printf '  %sscope: tools/tui/bun.lock (%s npm packages)%s\n' "$DIM" \
				"$(osv-scanner scan source -L tools/tui/bun.lock 2>/dev/null \
					| sed -n 's/.*found \([0-9]*\) packages.*/\1/p' | head -1)" "$RESET"
			osv-scanner scan source -L tools/tui/bun.lock 2>/dev/null \
				| grep -vE '^(Starting filesystem walk|End status:|Scanned )' || true
			# osv-scanner exits 1 when it finds something; ask again for the status
			# alone rather than parsing the table above.
			osv-scanner scan source -L tools/tui/bun.lock >/dev/null 2>&1 || rc=1
		fi
	else
		missing osv-scanner "brew install osv-scanner" || rc=1
	fi

	# pip-audit reads a requirements file, not a pyproject, so the declared dependencies are
	# lifted out of the [project] table into one.
	if [ -f integration/homeassistant/pyproject.toml ]; then
		if have pip-audit; then
			local req
			# Every step below is checked. An earlier version let mktemp fail (a full
			# disk) and carried on to report the gate green having audited nothing —
			# the same silent pass this whole file exists to prevent, reintroduced in
			# the file itself.
			if ! req="$(mktemp -t oa-req.XXXXXX)"; then
				printf '  %s%s%s could not create a temp file for the dependency list\n' \
					"$RED" "$CRS" "$RESET"
				return 1
			fi
			if ! python3 -c '
import tomllib
with open("integration/homeassistant/pyproject.toml", "rb") as f:
    d = tomllib.load(f)
for dep in d.get("project", {}).get("dependencies", []):
    print(dep)' > "$req" 2>/dev/null; then
				printf '  %s%s%s could not read the dependencies out of pyproject.toml\n' \
					"$RED" "$CRS" "$RESET"
				rm -f "$req"
				return 1
			fi
			if [ -s "$req" ]; then
				printf '  %sscope: integration/homeassistant (%s python deps)%s\n' \
					"$DIM" "$(wc -l < "$req" | tr -d ' ')" "$RESET"
				pip-audit --requirement "$req" --progress-spinner off 2>&1 \
					| sed 's/^/  /' || rc=1
			else
				printf '  %s%s%s pyproject.toml declared no dependencies to audit\n' \
					"$RED" "$CRS" "$RESET"
				rc=1
			fi
			rm -f "$req"
		else
			missing pip-audit "pipx install pip-audit" || rc=1
		fi
	fi

	[ "$rc" = 0 ] && printf '  %s%s%s no known-vulnerable or malicious packages\n' \
		"$GRN" "$CHK" "$RESET"
	return "$rc"
}

# ---- gates that live in their own script -----------------------------------
# Each is big enough to want its own file (the web gate parses HTML, the ct gate compiles and
# runs a harness under valgrind), but they dispatch through here so there is still one entry
# point that CI, `make security` and verify.sh all share.
gate_web() { scripts/security-web.sh; }
gate_esp() { scripts/security-workspace.sh esp; }
gate_attest() { scripts/security-attest.sh workflow; }

# ct is the one gate that can report neither pass nor fail. There is no valgrind for
# darwin/arm64, so on the primary dev machine the honest answer is "not checked here" — exit 2,
# which verify.sh renders as skip-tool rather than as a pass. CI runs linux and never sees it.
gate_ct() {
	scripts/security-ct.sh
	local rc=$?
	[ "$rc" = 2 ] && return 2
	return "$rc"
}

# ---- dispatch --------------------------------------------------------------
run_one() {
	case "$1" in
	secrets) gate_secrets ;;
	mal-diff) gate_maldiff ;;
	semgrep) gate_semgrep ;;
	deps) gate_deps ;;
	web) gate_web ;;
	ct) gate_ct ;;
	esp) gate_esp ;;
	attest) gate_attest ;;
	*)
		echo "security.sh: unknown gate '$1'" >&2
		echo "  known: secrets mal-diff semgrep deps web ct esp attest" >&2
		return 2
		;;
	esac
}

GATES=("$@")
[ ${#GATES[@]} -gt 0 ] || GATES=(secrets mal-diff semgrep deps web ct esp attest)

failed=()
skipped=()
for g in "${GATES[@]}"; do
	run_one "$g"
	case $? in
	0) ;;
	# 2 is "this host cannot run the check", not "the check passed". It is reported separately
	# and does not fail the run, because no install fixes it — but it is never silent, because a
	# gate you did not notice skipping is how a green sweep meets a red CI.
	2) skipped+=("$g") ;;
	*) failed+=("$g") ;;
	esac
done

printf '\n'
if [ ${#skipped[@]} -gt 0 ]; then
	printf '%s~ not checked on this host: %s (CI runs them)%s\n' \
		"$YEL" "${skipped[*]}" "$RESET"
fi
if [ ${#failed[@]} -gt 0 ]; then
	printf '%s%s security: %s%s\n\n' "$RED" "$CRS" "${failed[*]}" "$RESET"
	exit 1
fi
# Nothing was actually checked. Exiting 0 here is only correct when something else passed —
# `make security` should not go red because this Mac has no valgrind. But scripts/verify.sh runs
# one gate per invocation, and there a 0 makes "could not check" render as a green row. Propagate
# the skip so the caller can tell the two apart.
if [ ${#skipped[@]} -eq ${#GATES[@]} ]; then
	exit 2
fi
printf '%s%s security: %s%s\n\n' "$GRN" "$CHK" "${GATES[*]}" "$RESET"
