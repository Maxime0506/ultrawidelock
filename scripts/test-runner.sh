#!/usr/bin/env bash
#
# Pretty umbrella runner for every host-side suite: one banner, live per-check
# rows, a per-suite summary table, and suite timings. The suites themselves are
# unchanged — this only orchestrates and renders their existing output:
#
#   firmware (C host)      tests/host/run.sh        the KAT suite + the lab python suite
#   shared core (C host)   ports/esp32/test/run.sh  reader/stepup/crypto/... stages
#   web twin (drift)       web-twin/check_constants.py
#
# Default: suites run in parallel, output replayed in order when done.
# SERIAL=1 streams them live, one at a time. SUITES="firmware shared" scopes.
# Exit is nonzero if any suite fails. Colour off when not a TTY or NO_COLOR.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# ---- glyphs + colour ------------------------------------------------------
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' CYAN=$'\033[36m' GRN=$'\033[32m'
	RED=$'\033[31m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" ARR="▸" DOT="•"
	TL="╭" TR="╮" BL="╰" BR="╯" HZ="─" VT="│"
else
	BOLD="" DIM="" CYAN="" GRN="" RED="" RESET=""
	CHK="+" CRS="x" ARR=">" DOT="*"
	TL="+" TR="+" BL="+" BR="+" HZ="-" VT="|"
fi

W_IN=66 # inner width of the banner + summary box

hr() { # <char> <count>
	local out="" i
	for ((i = 0; i < $2; i++)); do out+="$1"; done
	printf '%s' "$out"
}

dlen() { # display length in characters (multibyte-aware), not bytes
	printf '%s' "$1" | wc -m | tr -d ' '
}

center() { # <text> — centred to W_IN
	local t="$1" len pad
	len="$(dlen "$t")"
	pad=$(((W_IN - len) / 2))
	printf '%*s%s%*s' "$pad" "" "$t" "$((W_IN - pad - len))" ""
}

boxed() { # <plain-row-content already W_IN wide, may hold ANSI>
	printf '  %s%s%s%s%s%s%s\n' "$CYAN$BOLD" "$VT" "$RESET" "$1" "$RESET" "$CYAN$BOLD$VT" "$RESET"
}

banner() {
	printf '\n  %s%s%s%s%s\n' "$CYAN$BOLD" "$TL" "$(hr "$HZ" "$W_IN")" "$TR" "$RESET"
	boxed "$BOLD$(center "OPENALIRO  $DOT  host-side test suites")"
	boxed "$DIM$(center "firmware KATs (C)   $DOT   shared core (C)   $DOT   web twin (drift)")"
	printf '  %s%s%s%s%s\n\n' "$CYAN$BOLD" "$BL" "$(hr "$HZ" "$W_IN")" "$BR" "$RESET"
}

# ---- suite definitions ----------------------------------------------------
suite_cmd() {
	case "$1" in
	firmware) echo "bash tests/host/run.sh" ;;
	shared) echo "bash ports/esp32/test/run.sh" ;;
	webtwin) echo "python3 web-twin/check_constants.py" ;;
	esac
}

suite_label() {
	case "$1" in
	firmware) echo "firmware (C host)" ;;
	shared) echo "shared core (C host)" ;;
	webtwin) echo "web twin (drift)" ;;
	esac
}

# passed/failed counts from a suite's captured output. Harnesses differ, so
# count the universal per-check rows plus each harness's own totals line.
suite_counts() { # <outfile> -> "passed failed"
	awk '
		/^[[:space:]]+ok[[:space:]]/    { p++ }
		/^[[:space:]]+FAIL[[:space:]]/  { f++ }
		/^Ran [0-9]+ tests?/            { p += $2 }
		# unittest skips are not passes: "OK (skipped=9)" et al.
		/skipped=[0-9]+/ {
			if (match($0, /skipped=[0-9]+/)) {
				p -= substr($0, RSTART + 8, RLENGTH - 8)
			}
		}
		/TOTAL[[:space:]]+[0-9]+[[:space:]]+✓/ { p += $2 }
		# side test binaries: "  uwb-driver: PASS (194 checks — ...)"
		/: PASS \([0-9]+ checks/ {
			if (match($0, /\([0-9]+ checks/)) {
				p += substr($0, RSTART + 1, RLENGTH - 8)
			}
		}
		/constants? verified/           { p += $1 }
		END { printf "%d %d", p + 0, f + 0 }
	' "$1"
}

# live rendering of one suite output line (streaming + replay)
render_line() { # <line>
	local l="$1"
	case "$l" in
	"  ok   "*) printf '    %s%s%s   %s\n' "$GRN" "$CHK" "$RESET" "${l#  ok   }" ;;
	"  FAIL "*) printf '    %s%s%s   %s\n' "$RED" "$CRS" "$RESET" "${l#  FAIL }" ;;
	"== "*)
		l="${l#== }"
		printf '  %s%s%s %s\n' "$CYAN" "$ARR" "$RESET" "${l% ==}"
		;;
	esac
}

run_suite() { # <suite> <outfile> <metafile>
	local s="$1" out="$2" meta="$3" cmd t0 t1 rc=0
	cmd="$(suite_cmd "$s")"
	t0=$(date +%s)
	if [[ "${SERIAL:-0}" == "1" ]]; then
		printf '\n  %s%s%s %s\n' "$CYAN" "$ARR" "$RESET" "$(suite_label "$s")"
		# shellcheck disable=SC2086 # cmd is a fixed two-word recipe from suite_cmd
		$cmd >"$out" 2>&1 &
		local pid=$!
		tail -f -n +1 "$out" 2>/dev/null | while IFS= read -r line; do
			render_line "$line"
		done &
		local tpid=$!
		wait "$pid" || rc=$?
		sleep 0.2
		kill "$tpid" 2>/dev/null || true
	else
		# shellcheck disable=SC2086
		$cmd >"$out" 2>&1 || rc=$?
	fi
	t1=$(date +%s)
	read -r passed failed <<<"$(suite_counts "$out")"
	printf '%s|%d|%d|%d|%d\n' "$s" "$passed" "$failed" "$((t1 - t0))" "$rc" >"$meta"
}

# ---- run ------------------------------------------------------------------
banner

SEL="${SUITES:-firmware shared webtwin}"
declare -a NAMES OUTS METAS PIDS
n=0
for s in $SEL; do
	NAMES[n]="$s"
	OUTS[n]="$(mktemp -t oa-suite-out.XXXXXX)"
	METAS[n]="$(mktemp -t oa-suite-meta.XXXXXX)"
	n=$((n + 1))
done
trap 'rm -f "${OUTS[@]}" "${METAS[@]}"' EXIT

if [[ "${SERIAL:-0}" == "1" ]]; then
	for i in $(seq 0 $((n - 1))); do
		run_suite "${NAMES[i]}" "${OUTS[i]}" "${METAS[i]}"
	done
else
	printf '  %s%s%s %d suites in parallel (SERIAL=1 streams them live, one at a time)\n' \
		"$CYAN" "$ARR" "$RESET" "$n"
	for i in $(seq 0 $((n - 1))); do
		run_suite "${NAMES[i]}" "${OUTS[i]}" "${METAS[i]}" &
		PIDS[i]=$!
	done
	for i in $(seq 0 $((n - 1))); do
		wait "${PIDS[i]}" || true
	done
	for i in $(seq 0 $((n - 1))); do
		printf '\n'
		while IFS= read -r line; do
			render_line "$line"
		done <"${OUTS[i]}"
	done
fi

# ---- summary table --------------------------------------------------------
# Row layout (plain widths sum to W_IN=66):
#  ' ' mark(1) '  ' label(24) '  ' passed(6) '  ' failed(6) '  ' time(8) pad(12)
row() { # <mark-colored> <label> <passed-col> <failed-col> <time>
	boxed "$(printf ' %s  %-24s  %s  %s  %8s%12s' "$1" "$2" "$3" "$4" "$5" "")"
}

tp=0 tf=0 tt=0 bad=0
printf '\n  %s%s%s%s%s\n' "$CYAN$BOLD" "$TL" "$(hr "$HZ" "$W_IN")" "$TR" "$RESET"
row " " "${BOLD}Suite${RESET}$(printf '%*s' 19 '')" "${BOLD}Passed${RESET}" \
	"${BOLD}Failed${RESET}" "Time"
boxed " $DIM$(hr "$HZ" $((W_IN - 2)))$RESET "
for i in $(seq 0 $((n - 1))); do
	IFS='|' read -r s passed failed secs rc <"${METAS[i]}"
	mark="$GRN$CHK$RESET"
	fcol="$GRN"
	if [[ "$rc" != 0 || "$failed" != 0 ]]; then
		mark="$RED$CRS$RESET" fcol="$RED" bad=1
	fi
	tp=$((tp + passed)) tf=$((tf + failed))
	[[ "$secs" -gt "$tt" ]] && tt=$secs
	row "$mark" "$(suite_label "$s")" \
		"$(printf '%s%6d%s' "$GRN" "$passed" "$RESET")" \
		"$(printf '%s%6d%s' "$fcol" "$failed" "$RESET")" "${secs}s"
done
boxed " $DIM$(hr "$HZ" $((W_IN - 2)))$RESET "
fcol="$GRN"
[[ "$tf" != 0 ]] && fcol="$RED"
row "$DIM$DOT$RESET" "Total" "$(printf '%s%6d%s' "$GRN$BOLD" "$tp" "$RESET")" \
	"$(printf '%s%6d%s' "$fcol" "$tf" "$RESET")" "${tt}s"
printf '  %s%s%s%s%s\n\n' "$CYAN$BOLD" "$BL" "$(hr "$HZ" "$W_IN")" "$BR" "$RESET"

# slowest suites (our C harnesses don't stamp per-test times; suites do)
printf '  %sslowest:%s\n' "$DIM" "$RESET"
for i in $(seq 0 $((n - 1))); do
	IFS='|' read -r s _ _ secs _ <"${METAS[i]}"
	printf '%s %s\n' "$secs" "$(suite_label "$s")"
done | sort -rn | head -3 | while read -r secs label; do
	printf '    %s%6ss  %s%s\n' "$DIM" "$secs" "$label" "$RESET"
done
printf '\n'

if [[ "$bad" == 0 ]]; then
	printf '  %s%s All host-side suites passed.%s\n' "$GRN" "$CHK" "$RESET"
	printf '  %sHardware-in-loop validation (DK/ESP32 + iPhone) runs separately.%s\n\n' \
		"$DIM" "$RESET"
else
	printf '  %s%s Suite failure — scroll up for the first %s row.%s\n\n' "$RED" "$CRS" "$CRS" "$RESET"
	exit 1
fi
