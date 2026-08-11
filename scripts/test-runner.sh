#!/usr/bin/env bash
#
# test-runner.sh — run every host-side suite, print failures and a summary.
#
#   firmware   tests/host/run.sh                the KAT suite
#   shared     tests/shared/run.sh              portable core + ESP port stages
#   sdk        tests/sdk/run.sh                 installed C package consumer
#   drift      drift_check.py + patch ID self-test
#   seam       tests/tooling/uwb_seam_check.sh  no call bypasses the STS seam
#   purity     tests/tooling/port_purity_check.sh  one source, one OS per port
#
# Default: suites run in parallel, failures replayed when done. SERIAL=1 streams
# full output one suite at a time. SUITES="firmware shared" scopes. Exit is
# nonzero if any suite fails.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

suite_cmd() {
	case "$1" in
	firmware) echo "bash tests/host/run.sh" ;;
	shared) echo "bash tests/shared/run.sh" ;;
	sdk) echo "bash tests/sdk/run.sh" ;;
	drift) echo "bash tests/tooling/drift_suite.sh" ;;
	seam) echo "bash tests/tooling/uwb_seam_check.sh" ;;
	purity) echo "bash tests/tooling/port_purity_check.sh" ;;
	esac
}

suite_label() {
	case "$1" in
	firmware) echo "firmware (C host)" ;;
	shared) echo "shared core (C host)" ;;
	sdk) echo "SDK package (CMake)" ;;
	drift) echo "constant drift" ;;
	seam) echo "uwb seam" ;;
	purity) echo "port purity" ;;
	esac
}

# passed/failed counts from a suite's captured output. Harnesses differ, so
# count the universal per-check rows plus each harness's own totals line.
suite_counts() { # <outfile> -> "passed failed"
	awk '
		/^[[:space:]]+ok[[:space:]]/    { p++ }
		/^[[:space:]]+FAIL[[:space:]]/  { f++ }
		/^Ran [0-9]+ tests?/            { p += $2 }
		/skipped=[0-9]+/ {
			if (match($0, /skipped=[0-9]+/)) {
				p -= substr($0, RSTART + 8, RLENGTH - 8)
			}
		}
		/TOTAL[[:space:]]+[0-9]+[[:space:]]+✓/ { p += $2 }
		/: PASS \([0-9]+ checks/ {
			if (match($0, /\([0-9]+ checks/)) {
				p += substr($0, RSTART + 1, RLENGTH - 8)
			}
		}
		/constants? verified/           { p += $1 }
		END { printf "%d %d", p + 0, f + 0 }
	' "$1"
}

run_suite() { # <suite> <outfile> <metafile>
	local s="$1" out="$2" meta="$3" cmd t0 t1 rc=0
	cmd="$(suite_cmd "$s")"
	t0=$(date +%s)
	if [[ "${SERIAL:-0}" == "1" ]]; then
		printf '\n== %s ==\n' "$(suite_label "$s")"
		# shellcheck disable=SC2086 # cmd is a fixed two-word recipe from suite_cmd
		$cmd 2>&1 | tee "$out" || rc=$?
	else
		# shellcheck disable=SC2086
		$cmd >"$out" 2>&1 || rc=$?
	fi
	t1=$(date +%s)
	read -r passed failed <<<"$(suite_counts "$out")"
	printf '%s|%d|%d|%d|%d\n' "$s" "$passed" "$failed" "$((t1 - t0))" "$rc" >"$meta"
}

SEL="${SUITES:-firmware shared sdk drift seam purity}"
declare -a NAMES OUTS METAS PIDS
n=0
for s in $SEL; do
	NAMES[n]="$s"
	OUTS[n]="$(mktemp -t oa-suite-out.XXXXXX)"
	METAS[n]="$(mktemp -t oa-suite-meta.XXXXXX)"
	n=$((n + 1))
done
trap 'rm -f "${OUTS[@]}" "${METAS[@]}"' EXIT

printf '\n  ultrawidelock · host-side test suites\n'
if [[ "${SERIAL:-0}" == "1" ]]; then
	for i in $(seq 0 $((n - 1))); do
		run_suite "${NAMES[i]}" "${OUTS[i]}" "${METAS[i]}"
	done
else
	for i in $(seq 0 $((n - 1))); do
		run_suite "${NAMES[i]}" "${OUTS[i]}" "${METAS[i]}" &
		PIDS[i]=$!
	done
	for i in $(seq 0 $((n - 1))); do
		wait "${PIDS[i]}" || true
	done
	# Replay only what needs eyes: the FAIL rows of any failing suite.
	for i in $(seq 0 $((n - 1))); do
		IFS='|' read -r _ _ failed _ rc <"${METAS[i]}"
		if [[ "$rc" != 0 || "$failed" != 0 ]]; then
			printf '\n== %s ==\n' "$(suite_label "${NAMES[i]}")"
			grep -E '^[[:space:]]+FAIL[[:space:]]|RESULT: FAIL|error|Error' "${OUTS[i]}" | head -40 || true
		fi
	done
fi

printf '\n  %-24s %8s %8s %6s\n' "Suite" "Passed" "Failed" "Time"
tp=0 tf=0 tt=0 bad=0
for i in $(seq 0 $((n - 1))); do
	IFS='|' read -r s passed failed secs rc <"${METAS[i]}"
	mark="+"
	if [[ "$rc" != 0 || "$failed" != 0 ]]; then mark="x" bad=1; fi
	tp=$((tp + passed)) tf=$((tf + failed))
	[[ "$secs" -gt "$tt" ]] && tt=$secs
	printf '  %s %-22s %8d %8d %5ss\n' "$mark" "$(suite_label "$s")" "$passed" "$failed" "$secs"
done
printf '  %s %-22s %8d %8d %5ss\n' "*" "Total" "$tp" "$tf" "$tt"

if [[ "$bad" == 0 ]]; then
	printf '\n  + All host-side suites passed.\n'
	printf '  Hardware-in-loop validation (DK/ESP32 + iPhone) runs separately.\n\n'
else
	printf '\n  x Suite failure — FAIL rows above.\n\n'
	exit 1
fi
