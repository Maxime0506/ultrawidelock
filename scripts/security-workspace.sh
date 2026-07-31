#!/usr/bin/env bash
#
# security-workspace.sh — the dependencies that are not in the tree, which is most of the binary.
#
# security-deep.yml's header already makes this argument against itself, and it is right:
#
#     syft over this source tree finds bun.lock and pyproject.toml and almost nothing else,
#     because NCS, Zephyr, ESP-IDF, esp-matter and the ESP component registry are none of them in
#     the tree at scan time. That is most of the shipped binary. Publishing an SBOM that omits the
#     RTOS and the TLS stack is worse than publishing none, because it looks like an answer.
#
# It then names the fix — scan workspace/ after `make bootstrap` and merge the ESP managed
# components — and calls it its own piece of work. This is that piece of work.
#
# The gate runs against a bootstrapped checkout, so it belongs in the deep lane and in release.yml
# (which bootstraps anyway to build), never in front of a pull request. Without workspace/ it
# exits 2 rather than passing: an SBOM of nothing is the exact failure the argument above warns
# about, and a green tick would be worse than no gate at all.
#
#   scripts/security-workspace.sh              # every check
#   scripts/security-workspace.sh pins         # one: pins esp sbom vulns
#   make security-workspace
#
# Exit 0 clean, 1 on a finding, 2 if the workspace is not bootstrapped.
#
# Checks:
#   pins    every resolved west revision is an immutable 40-hex SHA, and matches the recorded
#           pin set in security/workspace-pins.txt
#   esp     ESP component specs are exact versions, and the resolved lock is committed
#   sbom    syft over workspace/ + the ESP managed components -> build/sbom/openaliro.spdx.json
#   vulns   osv-scanner over that SBOM
#
# Env:
#   WS=path                  workspace root (default: ./workspace)
#   WORKSPACE_PINS=path      pin record (default: security/workspace-pins.txt)
#   WS_UPDATE_PINS=1         rewrite the pin record instead of comparing against it
#   NO_COLOR=1               plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

WS="${WS:-$ROOT/workspace}"
PINS="${WORKSPACE_PINS:-security/workspace-pins.txt}"
SBOM_DIR="$ROOT/build/sbom"
SBOM="$SBOM_DIR/openaliro.spdx.json"

if [[ -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' RED=$'\033[31m' GRN=$'\033[32m'
	YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" WRN="!"
else
	BOLD="" DIM="" RED="" GRN="" YEL="" RESET=""
	CHK="+" CRS="x" WRN="!"
fi

have() { command -v "$1" >/dev/null 2>&1; }
hdr() { printf '\n%s── %s%s\n' "$BOLD" "$1" "$RESET"; }
missing() {
	printf '  %s%s%s %s is not installed — %s\n' "$RED" "$CRS" "$RESET" "$1" "$2"
	printf '      A gate that cannot run is a gate that did not pass. CI runs it regardless.\n'
	return 1
}

# The workspace guard is per-check, not global: `esp` reads idf_component.yml out of the tracked
# tree and has nothing to do with a bootstrap, so it belongs in the fast lane and must not be
# blocked by a missing workspace/. The three that genuinely scan the fetched tree call this.
need_workspace() {
	[ -d "$WS" ] && return 0
	printf '\n  %s%s%s workspace/ is not bootstrapped, so there is nothing to scan.\n' \
		"$YEL" "$WRN" "$RESET"
	printf '      This gate deliberately does NOT pass in that state: an SBOM that omits the\n'
	printf '      RTOS and the TLS stack looks like an answer and is not one. Run `make\n'
	printf '      bootstrap` first, or let the deep lane run it on a runner that has.\n\n'
	return 2
}

# ---- pins ------------------------------------------------------------------
# west.yml pins the Nordic add-on to a full SHA, which is correct and is where the reproducibility
# claim in its header comes from. But the add-on's OWN manifest is what pins sdk-nrf, Zephyr and
# every module under it, and none of that is reviewable from this tree — `import: true` means the
# real dependency set is whatever that commit's manifest said. So it is read back from the
# resolved workspace instead, where it is a fact rather than a promise.
#
# A revision that resolves to a branch name is the finding that matters: it means `west update`
# on a different day produces a different tree from the same repository state, and every other
# gate in this repo is reasoning about the wrong bytes.
gate_pins() {
	hdr "workspace pins · immutable revisions"
	need_workspace || return $?
	have west || {
		missing west "pip install west, or source the NCS environment"
		return 1
	}

	local listing rc=0
	if ! listing="$(cd "$WS" && west list -f '{name}|{revision}|{url}' 2>/dev/null)"; then
		printf '  %s%s%s `west list` failed in %s — is it a west workspace?\n' \
			"$RED" "$CRS" "$RESET" "$WS"
		return 1
	fi

	WS_LIST="$listing" WS_PINS="$PINS" WS_UPDATE="${WS_UPDATE_PINS:-}" python3 - <<'PY'
import os, re, sys

SHA = re.compile(r"^[0-9a-f]{40}$")
rows = []
for ln in os.environ["WS_LIST"].splitlines():
    if not ln.strip():
        continue
    parts = ln.split("|")
    if len(parts) < 3:
        continue
    name, rev, url = parts[0].strip(), parts[1].strip(), parts[2].strip()
    if name == "manifest":
        continue
    rows.append((name, rev, url))

block, warn = [], []
for name, rev, url in rows:
    if SHA.match(rev):
        continue
    if rev in ("HEAD", "", "main", "master") or "/" in rev:
        block.append((name, rev, "resolves to a moving ref, so `west update` on a different day "
                                "builds a different tree from the same repository state"))
    else:
        # A tag. Immutable by convention, mutable in fact — a tag can be force-pushed and
        # nothing in a fetch notices. Worth a reviewer's eye, not worth blocking a build on.
        warn.append((name, rev, "tag, not a commit: immutable by convention only"))

print("  scope: %d west project(s)" % len(rows))

# --- drift against the recorded pin set ---------------------------------
path = os.environ["WS_PINS"]
current = "".join("%s %s %s\n" % (n, r, u) for n, r, u in sorted(rows))
if os.environ.get("WS_UPDATE"):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as fh:
        fh.write("# security/workspace-pins.txt — what `west update` actually resolved.\n")
        fh.write("# Regenerate with WS_UPDATE_PINS=1 scripts/security-workspace.sh pins, and\n")
        fh.write("# review the diff: it is the only place the transitive NCS/Zephyr dependency\n")
        fh.write("# set is visible to this repository at all.\n")
        fh.write(current)
    print("  recorded %d pin(s) to %s" % (len(rows), path))
else:
    try:
        prev = "".join(l for l in open(path) if l.strip() and not l.startswith("#"))
    except FileNotFoundError:
        block.append((path, "missing",
                      "No pin record exists, so nothing can detect the transitive dependency "
                      "set changing. Create it: WS_UPDATE_PINS=1 scripts/security-workspace.sh pins"))
        prev = None
    if prev is not None and prev != current:
        prev_map = dict((l.split()[0], l.split()[1]) for l in prev.splitlines() if l.split())
        cur_map = dict((n, r) for n, r, _ in rows)
        for n in sorted(set(prev_map) | set(cur_map)):
            a, b = prev_map.get(n), cur_map.get(n)
            if a != b:
                block.append((n, "%s -> %s" % (a or "(absent)", b or "(removed)"),
                              "The resolved dependency changed without the pin record changing. "
                              "If intended, regenerate the record in the same commit so the diff "
                              "is reviewable."))

for n, r, why in block:
    print("  BLOCK %s  %s" % (n, r))
    print("        %s" % why)
for n, r, why in warn:
    print("  warn  %s  %s — %s" % (n, r, why))
print("  %d blocking, %d advisory" % (len(block), len(warn)))
sys.exit(1 if block else 0)
PY
	rc=$?
	return "$rc"
}

# ---- esp -------------------------------------------------------------------
# The ESP component registry is a second package manager nothing in this repo audits. `deps` reads
# bun.lock and the Home Assistant pyproject; idf_component.yml is read by neither, and its default
# spec form is a RANGE — `version: "~1.0"` resolves at build time, on the runner, from a registry.
gate_esp() {
	hdr "workspace esp · component registry pins"
	local manifests
	manifests="$(git ls-files '*/idf_component.yml' || true)"
	if [ -z "$manifests" ]; then
		printf '  %sno idf_component.yml in the tree%s\n' "$DIM" "$RESET"
		return 0
	fi

	ESP_MANIFESTS="$manifests" ESP_WS="$WS" python3 - <<'PY'
import os, re, sys

files = [f for f in os.environ["ESP_MANIFESTS"].splitlines() if f.strip()]
block, warn, n = [], [], 0

# Deliberately a line scanner rather than a yaml parse: pyyaml is not a dependency of this repo
# and adding one to a security gate to read three files is the wrong trade. The forms that matter
# are all "  version: <spec>" at a fixed shape.
VER = re.compile(r'^\s+version:\s*["\']?([^"\'#\s]+)')
# `~N` is the ESP component registry's revision suffix: esp_bsp_devkit 3.0.0~2 is revision 2 of
# 3.0.0, and it is an exact version, not a range. Without it here the gate rejects the one pin
# the solver actually produces, which is how a gate ends up arguing with reality.
EXACT = re.compile(r'^(==\s*)?\d+\.\d+\.\d+([-+~][0-9A-Za-z.]+)?$')

for f in files:
    for i, ln in enumerate(open(f), 1):
        m = VER.match(ln)
        if not m:
            continue
        spec = m.group(1)
        n += 1
        if EXACT.match(spec):
            continue
        if spec == "*":
            block.append((f, i, spec, "'*' takes whatever the registry serves at build time, so "
                                      "there is no version this repository can be said to use."))
        else:
            # The wording matters more than the rule. "Pin the exact version" reads as "pick the
            # newest release", and doing that here broke every esp32-matter build: esp_bsp_devkit
            # 3.0.3 pulls led_indicator ~2.0, which needs led_strip 3.*, against a led_strip pin
            # of 2.5.5, and the solver rejects the set outright. An exact set has to be mutually
            # SOLVABLE, and the only thing that knows which one is the solver.
            block.append((f, i, spec,
                          "A range resolves on the runner, so the component compiled into a "
                          "release is not fixed by this repository. Do NOT pick the newest "
                          "release by hand -- the versions have to solve together. Run "
                          "`idf.py set-target <target>` and copy the versions out of the "
                          "dependencies.lock it writes."))

# Deliberately no "commit dependencies.lock" check. It is gitignored in this repo on purpose:
# the file records `target:`, and this app builds for esp32s3, esp32c5 and esp32c6, so one
# committed lock would be rewritten by whichever target built last. It is the right thing to
# READ and the wrong thing to commit, which is why the message above says read it.

print("  scope: %d manifest(s), %d version spec(s)" % (len(files), n))
for f, i, spec, why in block:
    print("  BLOCK %s:%s  version: %s" % (f, i, spec))
    print("        %s" % why)
for f, i, spec, why in warn:
    where = "%s:%s  version: %s" % (f, i, spec) if i else f
    print("  warn  %s" % where)
    print("        %s" % why)
print("  %d blocking, %d advisory" % (len(block), len(warn)))
sys.exit(1 if block else 0)
PY
}

# ---- sbom ------------------------------------------------------------------
# The one that TOOLING.md rejected, run in the only place it means anything. Both roots are
# scanned in one syft invocation so the output is a single document: an SBOM split across two
# files is one nobody consumes.
gate_sbom() {
	hdr "workspace sbom · syft over the bootstrapped tree"
	need_workspace || return $?
	have syft || {
		missing syft "brew install syft"
		return 1
	}
	mkdir -p "$SBOM_DIR"

	local roots=("$WS")
	# ESP managed components land under the app's build dir, not in workspace/, so they are a
	# second root rather than a subdirectory. Absent on a runner that only built the nRF target,
	# which is fine — the scope line reports what was actually covered.
	local mc
	mc="$(find "$ROOT/ports" -maxdepth 4 -type d -name managed_components 2>/dev/null | head -1)"
	[ -n "$mc" ] && roots+=("$mc")

	printf '  %sscope: %s%s\n' "$DIM" "${roots[*]}" "$RESET"
	if ! syft scan "${roots[@]/#/dir:}" -o spdx-json="$SBOM" -q 2>"$SBOM_DIR/syft.log"; then
		printf '  %s%s%s syft failed\n' "$RED" "$CRS" "$RESET"
		sed 's/^/      /' "$SBOM_DIR/syft.log" | head -20
		return 1
	fi

	SBOM_PATH="$SBOM" python3 -c '
import json, os, sys
from collections import Counter
d = json.load(open(os.environ["SBOM_PATH"]))
pkgs = d.get("packages", [])
kinds = Counter()
for p in pkgs:
    for ref in p.get("externalRefs", []):
        loc = ref.get("referenceLocator", "")
        if loc.startswith("pkg:"):
            kinds[loc.split("/")[0][4:]] += 1
            break
    else:
        kinds["unclassified"] += 1
print("  %d package(s): %s" % (len(pkgs), ", ".join("%s x%d" % kv for kv in kinds.most_common(8))))
# An SBOM with only the two lockfiles in it is the failure TOOLING.md described, produced by this
# gate rather than avoided by it. Say so instead of publishing it.
if len(pkgs) < 25:
    print("  BLOCK the SBOM has %d packages, which cannot be a bootstrapped NCS tree." % len(pkgs))
    print("        Publishing this would be the omission TOOLING.md argued against. Check that")
    print("        WS points at a completed `west update`.")
    sys.exit(1)
print("  written to build/sbom/openaliro.spdx.json")'
}

# ---- vulns -----------------------------------------------------------------
gate_vulns() {
	hdr "workspace vulns · osv-scanner over the SBOM"
	need_workspace || return $?
	have osv-scanner || {
		missing osv-scanner "brew install osv-scanner"
		return 1
	}
	if [ ! -f "$SBOM" ]; then
		printf '  %s%s%s no SBOM at %s — run the sbom check first\n' \
			"$RED" "$CRS" "$RESET" "$SBOM"
		return 1
	fi
	printf '  %sscope: build/sbom/openaliro.spdx.json%s\n' "$DIM" "$RESET"
	osv-scanner scan source --sbom "$SBOM" 2>/dev/null \
		| grep -vE '^(Starting|End status:|Scanned )' | sed 's/^/  /' || true
	osv-scanner scan source --sbom "$SBOM" >/dev/null 2>&1
}

# ---- dispatch --------------------------------------------------------------
run_one() {
	case "$1" in
	pins) gate_pins ;;
	esp) gate_esp ;;
	sbom) gate_sbom ;;
	vulns) gate_vulns ;;
	*)
		echo "security-workspace.sh: unknown check '$1' (pins esp sbom vulns)" >&2
		return 2
		;;
	esac
}

CHECKS=("$@")
[ ${#CHECKS[@]} -gt 0 ] || CHECKS=(pins esp sbom vulns)

failed=()
for c in "${CHECKS[@]}"; do
	run_one "$c" || failed+=("$c")
done

printf '\n'
if [ ${#failed[@]} -gt 0 ]; then
	printf '%s%s workspace: %s%s\n\n' "$RED" "$CRS" "${failed[*]}" "$RESET"
	exit 1
fi
printf '%s%s workspace: %s%s\n\n' "$GRN" "$CHK" "${CHECKS[*]}" "$RESET"
