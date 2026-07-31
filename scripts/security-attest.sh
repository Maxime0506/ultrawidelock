#!/usr/bin/env bash
#
# security-attest.sh — can somebody who downloaded a release prove where it came from?
#
# Today: no. release.yml assembles the bundles and writes SHA256SUMS.txt, which answers "are these
# the bytes the release page listed" and not "did this repository's CI build them". Those are
# different questions, and the second is the one that matters for a project whose distribution
# path ends in a browser page calling navigator.serial. A SHA256SUMS.txt served from the same
# release as the artifacts it describes is signed by nothing; whoever could replace the .bin could
# replace the sums file in the same motion.
#
# The fix is one action and two permissions in release.yml (see INTEGRATION.md), producing a
# Sigstore-backed attestation that binds each artifact to the workflow, repository and commit that
# built it. This script is the other half: the part that runs outside CI and checks the CI half is
# real.
#
#   scripts/security-attest.sh workflow          # static: release.yml still emits attestations
#   scripts/security-attest.sh verify v0.4.0     # download a release and verify it end to end
#   make security-attest
#
# Two modes, because they answer to different failure modes. `workflow` needs no network and no
# release to exist, so it can sit in the fast lane and catch the attestation step being dropped in
# an edit — the way a security control usually dies. `verify` is what a user would run, and is the
# only thing that proves the control works rather than that it is configured.
#
# Exit 0 clean, 1 on a finding, 2 if the mode could not run.
#
# Env:
#   REPO=owner/name    default asxeem/openaliro
#   NO_COLOR=1         plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

REPO="${REPO:-asxeem/openaliro}"
WORKFLOW=".github/workflows/release.yml"

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

# ---- workflow --------------------------------------------------------------
# A line scanner over the workflow rather than a yaml parse, for the reason security-diff.sh gives
# about its own choices: the checks below are about the presence and shape of four specific
# things, and a yaml dependency in a security gate that reads one file is a worse trade than a
# regex that is honest about what it matches.
gate_workflow() {
	hdr "attest workflow · release.yml still produces provenance"
	if [ ! -f "$WORKFLOW" ]; then
		printf '  %s%s%s no %s\n' "$RED" "$CRS" "$RESET" "$WORKFLOW"
		return 1
	fi

	ATT_WF="$WORKFLOW" python3 - <<'PY'
import os, re, sys

src = open(os.environ["ATT_WF"]).read()
block, warn = [], []

# The action, pinned. An unpinned attestation action is a contradiction: the thing establishing
# provenance would itself have none.
m = re.search(r"actions/attest-build-provenance@([0-9a-f]{40}|v\d+)", src)
if not m:
    block.append(("no attest-build-provenance step",
                  "Nothing binds a released artifact to the workflow that built it, so "
                  "SHA256SUMS.txt is the only integrity claim and it is signed by nobody. "
                  "Add the step to the job that publishes the release."))
elif not re.fullmatch(r"[0-9a-f]{40}", m.group(1)):
    block.append(("attest-build-provenance pinned to %s, not a SHA" % m.group(1),
                  "Every other third-party action in this repository is pinned to a full commit "
                  "SHA. The action that establishes provenance is the last one that should be "
                  "an exception."))

# The two permissions it needs. Missing either makes the step fail at run time, which is a
# release-day discovery rather than a pull-request one.
for perm in ("attestations: write", "id-token: write"):
    if perm not in src:
        block.append(("release.yml does not grant `%s`" % perm,
                      "attest-build-provenance cannot mint an attestation without it, and the "
                      "failure surfaces only when a tag is pushed."))

# Coverage: every artifact the release publishes should be attested, not just the first one. This
# is a heuristic — it compares the count of upload-artifact steps against subject globs — and it
# warns rather than blocks, because the mapping is not mechanical.
uploads = len(re.findall(r"actions/upload-artifact@", src))
subjects = len(re.findall(r"subject-path:", src))
if m and subjects and uploads > subjects:
    warn.append(("%d upload-artifact step(s) but %d subject-path entr(ies)" % (uploads, subjects),
                 "Check that every published artifact is covered, not only the first bundle."))

# SHA256SUMS is still worth having, but it should not be the only claim.
if "sha256sum" in src and not m:
    warn.append(("SHA256SUMS.txt is the sole integrity artifact",
                 "It is served from the same release it describes, so it defends against "
                 "corruption in transit and against nothing else."))

print("  scope: %s" % os.environ["ATT_WF"])
for what, why in block:
    print("  BLOCK %s" % what)
    print("        %s" % why)
for what, why in warn:
    print("  warn  %s" % what)
    print("        %s" % why)
print("  %d blocking, %d advisory" % (len(block), len(warn)))
sys.exit(1 if block else 0)
PY
}

# ---- verify ----------------------------------------------------------------
# What a user would actually do, run against a real release. This is the check that proves the
# control works; `workflow` above only proves it is configured, and the two have failed
# independently often enough elsewhere to be worth separating.
gate_verify() {
	local tag="${1:-}"
	hdr "attest verify · a published release, end to end"
	if [ -z "$tag" ]; then
		printf '  %s%s%s no tag given: scripts/security-attest.sh verify <tag>\n' \
			"$YEL" "$WRN" "$RESET"
		return 2
	fi
	have gh || {
		printf '  %s%s%s gh is not installed — brew install gh\n' "$RED" "$CRS" "$RESET"
		printf '      `gh attestation verify` is the reference verifier; cosign works too but\n'
		printf '      needs the certificate identity spelled out by hand.\n'
		return 1
	}

	local dir
	dir="$(mktemp -d -t oa-attest.XXXXXX)" || return 1
	# shellcheck disable=SC2064  # $dir is expanded now on purpose
	trap "rm -rf '$dir'" RETURN

	printf '  %sscope: %s %s%s\n' "$DIM" "$REPO" "$tag" "$RESET"
	if ! gh release download "$tag" --repo "$REPO" --dir "$dir" >/dev/null 2>&1; then
		printf '  %s%s%s could not download release %s from %s\n' \
			"$RED" "$CRS" "$RESET" "$tag" "$REPO"
		return 1
	fi

	local nfail=0 nok=0
	# The sums file first, since a mismatch there means the rest is moot.
	if [ -f "$dir/SHA256SUMS.txt" ]; then
		if (cd "$dir" && sha256sum -c SHA256SUMS.txt >/dev/null 2>&1); then
			printf '  %s%s%s SHA256SUMS.txt matches the downloaded artifacts\n' \
				"$GRN" "$CHK" "$RESET"
		else
			printf '  %s%s%s SHA256SUMS.txt does NOT match\n' "$RED" "$CRS" "$RESET"
			nfail=$((nfail + 1))
		fi
	else
		printf '  %s%s%s no SHA256SUMS.txt in the release\n' "$YEL" "$WRN" "$RESET"
	fi

	# Then provenance, per artifact. --signer-workflow is the part that matters: without it a
	# valid attestation from any workflow in any repository the verifier trusts would pass, and
	# the check degrades to "this was signed by something".
	local f
	for f in "$dir"/*.zip "$dir"/*.bin "$dir"/*.hex; do
		[ -f "$f" ] || continue
		if gh attestation verify "$f" --repo "$REPO" \
			--signer-workflow "$REPO/.github/workflows/release.yml" >/dev/null 2>&1; then
			printf '  %s%s%s %s\n' "$GRN" "$CHK" "$RESET" "$(basename "$f")"
			nok=$((nok + 1))
		else
			printf '  %s%s%s %s — no valid provenance from release.yml\n' \
				"$RED" "$CRS" "$RESET" "$(basename "$f")"
			nfail=$((nfail + 1))
		fi
	done

	printf '  %d verified, %d failed\n' "$nok" "$nfail"
	[ "$nfail" -eq 0 ] && [ "$nok" -gt 0 ]
}

# ---- dispatch --------------------------------------------------------------
MODE="${1:-workflow}"
shift 2>/dev/null || true

case "$MODE" in
workflow) gate_workflow ;;
verify) gate_verify "${1:-}" ;;
*)
	echo "security-attest.sh: unknown mode '$MODE' (workflow | verify <tag>)" >&2
	exit 2
	;;
esac
rc=$?

printf '\n'
if [ "$rc" -eq 0 ]; then
	printf '%s%s attest: %s%s\n\n' "$GRN" "$CHK" "$MODE" "$RESET"
elif [ "$rc" -eq 2 ]; then
	printf '%s%s attest: %s did not run%s\n\n' "$YEL" "$WRN" "$MODE" "$RESET"
else
	printf '%s%s attest: %s%s\n\n' "$RED" "$CRS" "$MODE" "$RESET"
fi
exit "$rc"
