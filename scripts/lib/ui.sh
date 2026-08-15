#!/usr/bin/env bash
#
# ui.sh — the live progress display shared by the long-running Make targets.
#
# `make test` compiles eight separate host binaries before the first one runs,
# which is nearly two minutes during which the old scripts printed nothing at
# all. Silence and a hang look identical, so this file exists to keep the
# difference visible: a step counter, a percentage, a bar, the elapsed time and
# a spinner that keeps moving while a compiler is thinking.
#
#   . "$ROOT/scripts/lib/ui.sh"
#   ui_begin "host test suite" "core suite:75" "uwb driver:9" "crypto seams:3"
#   ui_run   "core suite"   stage_core
#   ui_run   "uwb driver"   stage_drv
#   ui_run   "crypto seams" stage_psa
#   ui_end
#
# The plan is the labels in order, each with an optional ":seconds" hint for how
# long that step usually takes. The percentage is weighted by those seconds
# rather than by step index, so it tracks the clock instead of jumping from 11%
# to 22% while the longest compile of the run sits under the bar. Real durations
# are recorded per run and replace the hints from then on.
#
# Two rules the rest of the repo depends on:
#
#   * The chrome goes to stderr, the wrapped command's output to stdout. So
#     `make test > log` still writes exactly the bytes it always did into the
#     log while the bar animates on the terminal, and a suite captured by
#     scripts/test-runner.sh keeps a countable stdout.
#   * The display never decides whether the build passed. ui_run returns the
#     command's exit status untouched, and every part of the UI itself is
#     wrapped so that a missing tput, an unwritable cache or a $TERM nobody has
#     heard of degrades the output instead of failing the build.
#
# Modes, picked in ui_begin and overridable with ULTRAWIDELOCK_UI=0|1:
#
#   fancy   stderr is a terminal: in-place bar, one row per finished step.
#   plain   a pipe, a file, CI, TERM=dumb: one line per step, no escapes, no
#           redraws. This is what test-runner.sh and GitHub Actions get.
#
# What the drawn mode does not handle: a step whose output is binary. It is read
# back a line at a time, so an embedded NUL would be dropped on the way through.
# Nothing here emits one, and plain mode never reads the output at all.
#
# Written for bash 3.2, the bash macOS ships and therefore the one
# `#!/usr/bin/env bash` resolves to here: no associative arrays, no
# EPOCHREALTIME, no printf %(%s)T, no dynamic {fd} redirections.
#
#   scripts/lib/ui.sh --self-test   # the edge cases below, asserted

if [ -n "${_ULTRAWIDELOCK_UI_LOADED:-}" ]; then
	return 0
fi
_ULTRAWIDELOCK_UI_LOADED=1

# ---- capability detection ---------------------------------------------------

# Terminal width. Read once and then again on SIGWINCH rather than on every
# frame: the paint loop runs ten times a second for as long as the build does,
# and a `tput` fork per frame is a measurable tax on the compile it is drawn
# over. Every source can fail: tput needs a $TERM it knows, COLUMNS is only
# exported by interactive shells.
_ui_width() {
	local c=
	c="$(tput cols 2>/dev/null)" || c=
	case "$c" in '' | *[!0-9]*) c="${COLUMNS:-}" ;; esac
	case "$c" in '' | *[!0-9]*) c=80 ;; esac
	# Below ~20 columns there is nothing to lay out; clamp rather than emit
	# negative padding widths.
	[ "$c" -lt 20 ] && c=20
	printf '%s' "$c"
}

# UTF-8 or nothing: the block-drawing bar and the braille spinner are mojibake
# in a C locale, and this is exactly the case that makes a "beautiful" runner
# look broken in someone else's terminal.
_ui_unicode() {
	case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
	*UTF-8* | *UTF8* | *utf-8* | *utf8*) return 0 ;;
	esac
	return 1
}

# ---- state ------------------------------------------------------------------

_ui_mode=plain    # fancy | plain
_ui_cols=80       # terminal width, refreshed on SIGWINCH
# Glyphs default to the ASCII set so a caller that paints before ui_attach gets
# a plain frame rather than an unbound-variable abort under `set -u`.
_ui_full='#' _ui_empty='-' _ui_ok='+' _ui_bad='x' _ui_dot='.'
_ui_spin=('|' '/' '-' '\')
_ui_c= _ui_g= _ui_r= _ui_b= _ui_d= _ui_z=
_ui_total=0       # planned steps
_ui_i=0           # steps started
_ui_t0=0          # run start, epoch seconds
_ui_step_t0=0     # current step start
_ui_label=        # current step label
_ui_pct=0         # last painted percentage, never allowed to go backwards
_ui_frame=0       # spinner index
_ui_fail=0        # 1 once any step has failed
_ui_tmp=          # capture file for the running step
_ui_pid=          # the running step's subshell, for the cleanup trap
_ui_pending=      # a line the child has not terminated yet
_ui_key=          # timing-cache name
_ui_title=
_ui_totw=0        # sum of the planned steps' weights, seconds
_ui_donew=0       # weight of the steps that have finished
_ui_curw=0        # weight of the running step
_ui_have_cache=0  # 1 when the weights are measured rather than guessed
_ui_started=0     # 1 between ui_begin and ui_end, for the cleanup trap
_ui_plan=()       # planned labels, in order
_ui_hint=()       # "label:seconds" hints from the plan, parallel to _ui_plan
_ui_weight=()     # weight actually used per step, parallel to _ui_plan
_ui_done_label=() # finished labels + seconds, written back to the cache
_ui_done_secs=()
_ui_cache_lbl=()  # what the cache held when this run started, for the merge
_ui_cache_secs=()

# ---- glyphs and colour ------------------------------------------------------

_ui_glyphs() {
	if _ui_unicode; then
		_ui_full='█' _ui_empty='░' _ui_ok='✓' _ui_bad='✗' _ui_dot='·'
		_ui_spin=(⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏)
	else
		_ui_full='#' _ui_empty='-' _ui_ok='+' _ui_bad='x' _ui_dot='.'
		_ui_spin=('|' '/' '-' '\')
	fi
	# Same test the rest of the repo uses (tests/tooling/*.sh, mk/extras.mk),
	# against stderr because that is where the chrome goes.
	if [ -t 2 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != dumb ]; then
		_ui_c=$'\033[36m' _ui_g=$'\033[32m' _ui_r=$'\033[31m'
		_ui_b=$'\033[1m' _ui_d=$'\033[2m' _ui_z=$'\033[0m'
	else
		_ui_c= _ui_g= _ui_r= _ui_b= _ui_d= _ui_z=
	fi
}

# ---- formatting -------------------------------------------------------------

# Whole seconds is all we have: bash 3.2 has no EPOCHREALTIME and BSD date has
# no %N, so a sub-second duration prints as "<1s" rather than a fake "0.0s".
_ui_dur() {
	local s="${1:-0}"
	if [ "$s" -le 0 ]; then
		printf '<1s'
	elif [ "$s" -lt 60 ]; then
		printf '%ds' "$s"
	else
		printf '%dm%02ds' "$((s / 60))" "$((s % 60))"
	fi
}

_ui_plural() { # <count> <noun>
	if [ "$1" = 1 ]; then
		printf '%s %s' "$1" "$2"
	else
		printf '%s %ss' "$1" "$2"
	fi
}

_ui_repeat() { # <string> <count>
	local out= i=0
	while [ "$i" -lt "$2" ]; do
		out="$out$1"
		i=$((i + 1))
	done
	printf '%s' "$out"
}

# ---- the timing cache -------------------------------------------------------
#
# A step counter alone makes 8/12 look like 67% even when the last four steps
# are the slow ones. So the previous run's per-step seconds are kept and used as
# weights, which is what makes the percentage track the clock instead of the
# step index. It is a cache in the strict sense: absent, stale or corrupt, every
# step falls back to an equal share and the display is merely less accurate.

_ui_cache_file() {
	local root="${ULTRAWIDELOCK_BUILD_ROOT:-}"
	[ -n "$root" ] || root="${ROOT:-.}/build"
	printf '%s/_ui/%s.tsv' "$root" "$_ui_key"
}

# Weight for a label, in order of preference: its duration in the last run, the
# caller's own "label:seconds" hint, the mean of the durations we do know, then
# 10s. The hint is what stops the very first build -- the one on a fresh clone,
# where the cache cannot exist yet -- from spending a minute inside a step whose
# share of the bar is 1/9th.
_ui_load_weights() {
	local f n=0 sum=0 i lbl secs
	f="$(_ui_cache_file)"
	# Globals: _ui_save_weights merges what it did not re-measure back in.
	_ui_cache_lbl=()
	_ui_cache_secs=()
	local clbl=() csecs=()
	if [ -r "$f" ]; then
		while IFS=$'\t' read -r lbl secs; do
			# Skip anything that is not "<label><TAB><integer>": a
			# half-written cache must not produce arithmetic errors.
			[ -n "$lbl" ] || continue
			case "$secs" in '' | *[!0-9]*) continue ;; esac
			clbl[${#clbl[@]}]="$lbl"
			csecs[${#csecs[@]}]="$secs"
			_ui_cache_lbl[${#_ui_cache_lbl[@]}]="$lbl"
			_ui_cache_secs[${#_ui_cache_secs[@]}]="$secs"
			n=$((n + 1))
			sum=$((sum + secs))
		done <"$f" 2>/dev/null || true
	fi
	local mean=10
	if [ "$n" -gt 0 ]; then
		mean=$((sum / n))
		if [ "$mean" -lt 1 ]; then mean=1; fi
		_ui_have_cache=1
	fi
	# C-style loops throughout, not `seq 0 $((n - 1))`: BSD seq answers
	# `seq 0 -1` with "0 -1" rather than with nothing, so an empty plan would
	# index past the end of the array.
	_ui_totw=0
	local n_plan="${#_ui_plan[@]}" j
	for ((i = 0; i < n_plan; i++)); do
		local w="$mean"
		if [ "${_ui_hint[i]}" -gt 0 ]; then
			w="${_ui_hint[i]}"
			_ui_have_cache=1
		fi
		for ((j = 0; j < n; j++)); do
			if [ "${clbl[j]}" = "${_ui_plan[i]}" ]; then
				w="${csecs[j]}"
				break
			fi
		done
		if [ "$w" -lt 1 ]; then w=1; fi
		_ui_weight[i]="$w"
		_ui_totw=$((_ui_totw + w))
	done
	if [ "$_ui_totw" -lt 1 ]; then _ui_totw=1; fi
	# Explicit: a function whose last command is a false test returns 1, and
	# the callers run under `set -e`.
	return 0
}

# Best-effort, always: a read-only or missing build root costs the next run its
# accurate percentage and nothing else.
#
# What this run measured, merged over what the file already held. Merged rather
# than replaced because a run is often a subset -- `tests/host/cbmc.sh ccc_mac`
# proves one harness of three, an interrupted run finishes none of them -- and
# replacing would throw away the timings of every step that was not asked for
# this time, leaving the next full run to guess.
_ui_save_weights() {
	local f d i j keep
	[ "${#_ui_done_label[@]}" -gt 0 ] || return 0
	f="$(_ui_cache_file)"
	d="$(dirname "$f")"
	mkdir -p "$d" 2>/dev/null || return 0
	local n="${#_ui_done_label[@]}" m="${#_ui_cache_lbl[@]}"
	{
		for ((j = 0; j < m; j++)); do
			keep=1
			for ((i = 0; i < n; i++)); do
				if [ "${_ui_cache_lbl[j]}" = "${_ui_done_label[i]}" ]; then
					keep=0
					break
				fi
			done
			if [ "$keep" = 1 ]; then
				printf '%s\t%s\n' "${_ui_cache_lbl[j]}" "${_ui_cache_secs[j]}"
			fi
		done
		for ((i = 0; i < n; i++)); do
			printf '%s\t%s\n' "${_ui_done_label[i]}" "${_ui_done_secs[i]}"
		done
	} >"$f.$$" 2>/dev/null && mv -f "$f.$$" "$f" 2>/dev/null
	rm -f "$f.$$" 2>/dev/null || true
	return 0
}

# ---- painting ---------------------------------------------------------------

_ui_clear() {
	[ "$_ui_mode" = fancy ] || return 0
	printf '\r\033[2K' >&2
}

# Percentage, in integers, from the finished weight plus how far into the
# current step's own weight we are. Past its expected duration the step
# asymptotes towards its own boundary instead of overshooting into the next
# one -- a step that runs long should look slow, not finished.
_ui_percent() { # <seconds-in-step>
	local t="${1:-0}" w="$_ui_curw" base span frac pct
	[ "$w" -ge 1 ] || w=1
	base=$((_ui_donew * 1000 / _ui_totw))
	span=$((w * 1000 / _ui_totw))
	if [ "$t" -le "$w" ]; then
		frac=$((90 * t / w))
	else
		# Past the expected duration the step creeps through the last tenth of
		# its own span and never leaves it: at twice the weight 93%, at five
		# times 96%, never 100%. A step running long has to look slow, and a
		# bar that sits at 99% while real work continues is the lie this whole
		# file exists to stop telling.
		frac=$((90 + 9 * (t - w) / (t + w)))
	fi
	pct=$(((base + span * frac / 100) / 10))
	[ "$pct" -gt 99 ] && pct=99
	[ "$pct" -lt "$_ui_pct" ] && pct="$_ui_pct"
	_ui_pct="$pct"
	printf '%s' "$pct"
}

_ui_paint() {
	[ "$_ui_mode" = fancy ] || return 0
	local cols t pct spin meta bar barw fill vis label
	cols="$_ui_cols"
	t=$(($(date +%s) - _ui_step_t0))
	[ "$t" -lt 0 ] && t=0
	pct="$(_ui_percent "$t")"
	spin="${_ui_spin[$((_ui_frame % ${#_ui_spin[@]}))]}"
	_ui_frame=$((_ui_frame + 1))

	# A run with no plan and no cache behind it knows how many steps it has
	# taken and not how many are left; it says exactly that rather than putting
	# a percentage on a denominator it does not have.
	if [ "$_ui_total" -gt 0 ]; then
		meta="$_ui_i/$_ui_total $_ui_dot $(_ui_dur "$t")"
	else
		meta="step $_ui_i $_ui_dot $(_ui_dur "$t")"
	fi
	if [ "$_ui_have_cache" = 1 ] && [ "$_ui_total" -gt 0 ]; then
		local eta=$((_ui_totw - _ui_donew - t))
		[ "$eta" -gt 0 ] && meta="$meta $_ui_dot eta $(_ui_dur "$eta")"
	fi

	# Layout: "  S PPP% [bar] meta  label", bar and label absorbing whatever
	# room is left. Under ~46 columns the bar is dropped before the label is,
	# because the label is the part that says what is happening.
	barw=$((cols - 34 - ${#meta}))
	[ "$barw" -gt 28 ] && barw=28
	[ "$barw" -lt 8 ] && barw=0
	[ "$_ui_total" -gt 0 ] || barw=0
	bar=
	if [ "$barw" -gt 0 ]; then
		fill=$((pct * barw / 100))
		bar=" $_ui_c$(_ui_repeat "$_ui_full" "$fill")$_ui_z$_ui_d$(_ui_repeat "$_ui_empty" "$((barw - fill))")$_ui_z"
		vis=$((barw + 1))
	else
		vis=0
	fi

	# Visible columns consumed by everything except the label: two of indent,
	# spinner, space, four of percentage, the bar, two spaces, the meta block.
	vis=$((2 + 1 + 1 + 4 + vis + 2 + ${#meta}))
	label="$_ui_label"
	local room=$((cols - vis - 2))
	if [ "$room" -lt 1 ]; then
		label=
	elif [ "${#label}" -gt "$room" ]; then
		label="${label:0:$((room - 1))}…"
		_ui_unicode || label="${label:0:$((room - 1))}~"
	fi

	if [ "$_ui_total" -gt 0 ]; then
		printf '\r\033[2K  %s%s%s %s%3d%%%s%s  %s%s%s %s%s' \
			"$_ui_c" "$spin" "$_ui_z" "$_ui_b" "$pct" "$_ui_z" "$bar" \
			"$_ui_d" "$meta" "$_ui_z" "$label" "$_ui_z" >&2
	else
		printf '\r\033[2K  %s%s%s  %s%s%s %s%s' \
			"$_ui_c" "$spin" "$_ui_z" \
			"$_ui_d" "$meta" "$_ui_z" "$label" "$_ui_z" >&2
	fi
}

# A finished step's permanent row: mark, label, dotted leader, duration. Capped
# at 72 columns so a full-screen terminal does not stretch it to the horizon.
_ui_row() { # <mark> <colour> <label> <seconds>
	local cols w lead
	cols="$_ui_cols"
	[ "$cols" -gt 72 ] && cols=72
	local dur
	dur="$(_ui_dur "$4")"
	w=$((cols - 6 - ${#3} - ${#dur}))
	[ "$w" -lt 1 ] && w=1
	lead="$(_ui_repeat "$_ui_dot" "$w")"
	_ui_clear
	printf '  %s%s%s %s %s%s%s %s%s%s\n' \
		"$2" "$1" "$_ui_z" "$3" "$_ui_d" "$lead" "$_ui_z" "$_ui_d" "$dur" "$_ui_z" >&2
}

# ---- child output -----------------------------------------------------------

# One line of the wrapped command's output, on stdout, with the bar lifted out
# of the way first. stdout is deliberately untouched otherwise: what a suite
# printed before this file existed is what it prints now.
_ui_emit() {
	_ui_clear
	printf '%s\n' "$1"
}

# Read whatever the child has written since the last call. bash 3.2 has no
# {fd} redirections, so fd 9 is the fixed capture descriptor. `read` fails at
# end-of-file with the unterminated remainder still in $line, which is held in
# _ui_pending and completed by the next call rather than printed twice or
# dropped -- a compiler that dies mid-line still gets its last words out.
_ui_drain() {
	local line
	while IFS= read -r line <&9; do
		_ui_emit "$_ui_pending$line"
		_ui_pending=
	done
	_ui_pending="$_ui_pending$line"
}

_ui_flush() {
	_ui_drain
	if [ -n "$_ui_pending" ]; then
		_ui_emit "$_ui_pending"
		_ui_pending=
	fi
}

# ---- lifecycle --------------------------------------------------------------

_ui_cleanup() {
	[ "$_ui_started" = 1 ] || return 0
	_ui_started=0
	if [ "$_ui_mode" = fancy ]; then
		printf '\r\033[2K\033[?25h' >&2
	fi
	# ^C at a terminal reaches the whole foreground process group, so the
	# compiler under the bar has already had it. An explicit kill of this shell
	# has not, and leaves the step's wrapper behind writing into a file about to
	# be deleted; end it here.
	if [ -n "$_ui_pid" ]; then
		kill "$_ui_pid" 2>/dev/null
		_ui_pid=
	fi
	[ -n "$_ui_tmp" ] && rm -f "$_ui_tmp" 2>/dev/null
	_ui_save_weights
	return 0
}

# Mode, glyphs and the terminal-restoring traps, without a plan or a header.
# What a caller that drives its own loop -- scripts/test-runner.sh runs its
# suites in parallel and cannot be a sequence of ui_run calls -- needs before it
# can use ui_status.
ui_attach() {
	_ui_glyphs
	case "${ULTRAWIDELOCK_UI:-}" in
	0) _ui_mode=plain ;;
	1) _ui_mode=fancy ;;
	*)
		if [ -t 2 ] && [ "${TERM:-dumb}" != dumb ]; then
			_ui_mode=fancy
		else
			_ui_mode=plain
		fi
		;;
	esac
	_ui_t0="$(date +%s)"
	_ui_step_t0="$_ui_t0"
	_ui_started=1
	_ui_cols="$(_ui_width)"
	trap '_ui_cleanup' EXIT
	trap '_ui_cleanup; trap - INT; kill -INT $$' INT
	trap '_ui_cleanup; trap - TERM; kill -TERM $$' TERM
	# bash runs a trap between commands, and the paint loop is nothing but short
	# commands, so a window resized mid-build is picked up within a frame.
	trap '_ui_cols="$(_ui_width)"' WINCH
	if [ "$_ui_mode" = fancy ]; then
		printf '\033[?25l' >&2
	fi
}

# ui_clear -- erase the status line. Call it before printing anything yourself,
# so your line lands on a clean row rather than on top of the bar.
ui_clear() { _ui_clear; }

# ui_status <percent> <text> -- paint one in-place status line: spinner, that
# percentage, a bar, then the text. For loops that own their own progress
# arithmetic. Silent in plain mode, where an in-place line is just noise.
ui_status() {
	[ "$_ui_mode" = fancy ] || return 0
	local pct="$1" cols bar barw fill spin text room
	case "$pct" in '' | *[!0-9]*) pct=0 ;; esac
	[ "$pct" -gt 100 ] && pct=100
	shift
	text="$*"
	cols="$_ui_cols"
	spin="${_ui_spin[$((_ui_frame % ${#_ui_spin[@]}))]}"
	_ui_frame=$((_ui_frame + 1))
	barw=$((cols - 12 - ${#text}))
	[ "$barw" -gt 28 ] && barw=28
	[ "$barw" -lt 8 ] && barw=0
	bar=
	if [ "$barw" -gt 0 ]; then
		fill=$((pct * barw / 100))
		bar=" $_ui_c$(_ui_repeat "$_ui_full" "$fill")$_ui_z$_ui_d$(_ui_repeat "$_ui_empty" "$((barw - fill))")$_ui_z"
		room=$((cols - 10 - barw - 2))
	else
		room=$((cols - 10))
	fi
	if [ "$room" -lt 1 ]; then
		text=
	elif [ "${#text}" -gt "$room" ]; then
		text="${text:0:$room}"
	fi
	printf '\r\033[2K  %s%s%s %s%3d%%%s%s  %s' \
		"$_ui_c" "$spin" "$_ui_z" "$_ui_b" "$pct" "$_ui_z" "$bar" "$text" >&2
}

# ui_begin <title> [label ...] -- the labels are the plan, used for the step
# count and the weights. Passing none is allowed; the percentage then only moves
# at step boundaries.
ui_begin() {
	_ui_title="$1"
	shift
	_ui_plan=()
	_ui_hint=()
	local a lbl hint
	for a in "$@"; do
		# "label:90" carries a first-run duration hint; a label that ends
		# in a colon-and-non-digits is left exactly as written.
		lbl="$a"
		hint=0
		case "$a" in
		*:[0-9]*)
			case "${a##*:}" in
			'' | *[!0-9]*) ;;
			*)
				hint="${a##*:}"
				lbl="${a%:*}"
				;;
			esac
			;;
		esac
		_ui_plan[${#_ui_plan[@]}]="$lbl"
		_ui_hint[${#_ui_hint[@]}]="$hint"
	done
	_ui_total="${#_ui_plan[@]}"
	_ui_i=0
	_ui_pct=0
	_ui_fail=0
	_ui_donew=0
	_ui_t0="$(date +%s)"
	_ui_step_t0="$_ui_t0"

	# The cache is keyed on the calling script -- its directory as well as its
	# name, because tests/host/run.sh and tests/shared/run.sh would otherwise
	# both be "run" and would trade weights back and forth every build. A script
	# with two shapes of run sets ULTRAWIDELOCK_UI_KEY to keep them apart:
	# `make test-san` compiles the same sources several times slower than
	# `make test`, and one cache holding both is a cache holding neither.
	if [ -n "${ULTRAWIDELOCK_UI_KEY:-}" ]; then
		_ui_key="$ULTRAWIDELOCK_UI_KEY"
	else
		local src="${BASH_SOURCE[${#BASH_SOURCE[@]} - 1]:-ui}"
		_ui_key="$(basename "$(dirname "$src")")-$(basename "$src")"
	fi
	_ui_key="${_ui_key%.sh}"
	case "$_ui_key" in '' | *[!A-Za-z0-9._-]*) _ui_key=ui ;; esac

	# Mode, glyphs, traps. INT and TERM restore the terminal there and then die
	# the way the shell would have, so `make test` interrupted with ^C still
	# reports 130 upstream.
	ui_attach
	_ui_load_weights

	# No plan given: the previous run's cache is the plan. A script whose steps
	# are forty small compiles (tests/host/coverage.sh) should not have to
	# restate their names here, and a plan taken from the last run is one that
	# cannot fall out of step with the script. The first run of all, with no
	# cache to read, simply counts steps and shows no percentage.
	if [ "$_ui_total" = 0 ] && [ "${#_ui_cache_lbl[@]}" -gt 0 ]; then
		local k
		for ((k = 0; k < ${#_ui_cache_lbl[@]}; k++)); do
			_ui_plan[k]="${_ui_cache_lbl[k]}"
			_ui_hint[k]=0
		done
		_ui_total="${#_ui_plan[@]}"
		_ui_load_weights
	fi

	_ui_t0="$(date +%s)"
	_ui_step_t0="$_ui_t0"

	# No plan and no cache: the step count is not known yet, so the header says
	# the title alone rather than "0 steps".
	local count=
	if [ "$_ui_total" -gt 0 ]; then
		count=" $_ui_dot $(_ui_plural "$_ui_total" step)"
	fi
	if [ "$_ui_mode" = fancy ]; then
		printf '\n  %sultrawidelock%s  %s%s %s%s%s\n\n' \
			"$_ui_b" "$_ui_z" "$_ui_d" "$_ui_dot" "$_ui_title" \
			"$count" "$_ui_z" >&2
	else
		printf '\n  ultrawidelock . %s%s\n' "$_ui_title" "$count" >&2
	fi
}

# ui_run <label> <command> [args ...] -- run one step. Returns the command's
# exit status; on failure it also prints the footer, because the caller's
# `set -e` will end the script on the way back and ui_end will never run.
ui_run() {
	local rc=0
	ui_run_try "$@" || rc=$?
	if [ "$rc" != 0 ]; then
		ui_end "$rc" || true
		return "$rc"
	fi
	return 0
}

# ui_run_try -- ui_run for a caller that carries on past a failure and reports
# at the end itself (tests/host/cbmc.sh proves three harnesses and wants all
# three verdicts). Same row, same exit status, no footer; the failure is
# remembered, so a later ui_end still ends in red.
ui_run_try() {
	local label="$1"
	shift
	local rc=0 t0 secs i n_plan="${#_ui_plan[@]}"
	_ui_i=$((_ui_i + 1))
	_ui_label="$label"
	# A label the plan does not carry (a stale plan, or none at all) gets the
	# neutral weight rather than breaking the percentage.
	_ui_curw=10
	for ((i = 0; i < n_plan; i++)); do
		if [ "${_ui_plan[i]}" = "$label" ]; then
			_ui_curw="${_ui_weight[i]}"
			break
		fi
	done
	# A step past the end of the plan -- a stage added since the cache the plan
	# came from -- grows the total instead of printing 42/41.
	if [ "$_ui_total" -gt 0 ] && [ "$_ui_i" -gt "$_ui_total" ]; then
		_ui_total="$_ui_i"
		_ui_totw=$((_ui_totw + _ui_curw))
	fi
	t0="$(date +%s)"
	_ui_step_t0="$t0"

	if [ "$_ui_mode" != fancy ]; then
		# Plain mode runs the command with its streams untouched: no
		# capture file, no polling, no reordering. Whatever CI recorded
		# before is what CI records now, one announcement line ahead of
		# it. The " . " between the fields keeps this row clear of
		# test-runner.sh's counters, which claim lines starting with
		# "ok" or "FAIL".
		local pos="$_ui_i"
		[ "$_ui_total" -gt 0 ] && pos="$_ui_i/$_ui_total"
		printf '  [%s] %s\n' "$pos" "$label" >&2
		if "$@"; then rc=0; else rc=$?; fi
		secs=$(($(date +%s) - t0))
		if [ "$rc" = 0 ]; then
			printf '  [%s] %s . done in %s\n' \
				"$pos" "$label" "$(_ui_dur "$secs")" >&2
		else
			printf '  [%s] %s . FAILED (exit %s) after %s\n' \
				"$pos" "$label" "$rc" "$(_ui_dur "$secs")" >&2
		fi
	else
		_ui_tmp="$(mktemp -t ultrawidelock-ui.XXXXXX)" || _ui_tmp=
		if [ -z "$_ui_tmp" ]; then
			# No temp file, no capture: fall back to running the step
			# straight through rather than losing its output.
			_ui_clear
			if "$@"; then rc=0; else rc=$?; fi
		else
			local pid
			("$@") >"$_ui_tmp" 2>&1 &
			pid=$!
			_ui_pid="$pid"
			exec 9<"$_ui_tmp"
			_ui_pending=
			# One thread doing both jobs: drain what the child wrote,
			# repaint, sleep. A background painter would race this
			# loop for the cursor.
			while kill -0 "$pid" 2>/dev/null; do
				_ui_drain
				_ui_paint
				sleep 0.1 2>/dev/null || sleep 1
			done
			if wait "$pid"; then rc=0; else rc=$?; fi
			_ui_pid=
			_ui_flush
			exec 9<&-
			rm -f "$_ui_tmp" 2>/dev/null
			_ui_tmp=
		fi
		secs=$(($(date +%s) - t0))
		if [ "$rc" = 0 ]; then
			_ui_row "$_ui_ok" "$_ui_g" "$label" "$secs"
		else
			_ui_row "$_ui_bad" "$_ui_r" "$label" "$secs"
		fi
	fi

	if [ "$rc" = 0 ]; then
		_ui_done_label[${#_ui_done_label[@]}]="$label"
		_ui_done_secs[${#_ui_done_secs[@]}]="$secs"
		_ui_donew=$((_ui_donew + _ui_curw))
	else
		_ui_fail=1
	fi
	return "$rc"
}

# ui_note <text> -- a line of chrome between steps, cleared past the bar.
ui_note() {
	_ui_clear
	printf '  %s%s%s\n' "$_ui_d" "$*" "$_ui_z" >&2
}

# ui_end [exit-status] -- the footer. Returns the status it was given, so
# `ui_end` on its own is a no-op for a passing script.
ui_end() {
	local rc="${1:-0}" total
	total=$(($(date +%s) - _ui_t0))
	_ui_clear
	if [ "$rc" = 0 ] && [ "$_ui_fail" = 0 ]; then
		printf '\n  %s%s%s %s%s %s %s%s\n\n' \
			"$_ui_g" "$_ui_ok" "$_ui_z" "$_ui_d" \
			"$(_ui_plural "$_ui_i" step)" "$_ui_dot" \
			"$(_ui_dur "$total")" "$_ui_z" >&2
	else
		# The label is empty when nothing got as far as running: a plan that
		# named a step the caller then skipped, or a failure before the first.
		printf '\n  %s%s%s %sfailed at step %d/%d %s %s%s\n\n' \
			"$_ui_r" "$_ui_bad" "$_ui_z" "${_ui_label:+$_ui_label }" \
			"$_ui_i" "$_ui_total" \
			"$_ui_dot" "$(_ui_dur "$total")" "$_ui_z" >&2
	fi
	_ui_cleanup
	return "$rc"
}

# ---- self-test --------------------------------------------------------------
#
# The failure mode of a progress display is that it eats or reorders the output
# it is wrapping, and that shows up as a missing test, not as a broken bar. The
# properties below are the ones the rest of the repo relies on.

_ui_fixture() { # <case>, run in its own process by _ui_self_test
	case "$1" in
	pass)
		ui_begin "fixture" "one" "two"
		ui_run "one" printf 'a\nb\n'
		ui_run "two" printf 'c\n'
		ui_end
		;;
	fail)
		ui_begin "fixture" "one" "two"
		ui_run "one" printf 'a\n'
		ui_run "two" sh -c 'printf "boom\n"; exit 3'
		ui_end
		;;
	partial) # last line has no newline of its own
		ui_begin "fixture" "one"
		ui_run "one" printf 'x\ny-no-newline'
		ui_end
		;;
	volume) # 5000 lines through the drain loop, none lost, none doubled
		ui_begin "fixture" "one"
		ui_run "one" awk 'BEGIN { for (i = 1; i <= 5000; i++) print i }'
		ui_end
		;;
	args) # arguments survive word-splitting and quoting
		ui_begin "fixture" "one"
		ui_run "one" printf '%s\n' 'a b' '  c  ' '$d' 'e*f'
		ui_end
		;;
	hints) # ":90" is a weight, ":live" is part of the label
		ui_begin "fixture" "weighted:90" "cdk: live probe"
		ui_run "weighted" true
		ui_run "cdk: live probe" true
		ui_end
		;;
	both) # two steps, both measured
		ui_begin "fixture" "alpha" "beta"
		ui_run "alpha" true
		ui_run "beta" true
		ui_end
		;;
	subset) # only the second one, as `cbmc.sh <one-harness>` does
		ui_begin "fixture" "beta"
		ui_run "beta" true
		ui_end
		;;
	unplanned) # no plan at all: the cache is the plan, as coverage.sh does
		ui_begin "fixture"
		ui_run "alpha" true
		ui_run "beta" true
		ui_end
		;;
	esac
}

_ui_self_test() {
	local fails=0 rc
	# Globals, not locals: the EXIT trap fires after this function's frame is
	# gone, and `set -u` turns a dead local into a fatal error there.
	tmp_out="$(mktemp -t ultrawidelock-ui-t.XXXXXX)"
	tmp_err="$(mktemp -t ultrawidelock-ui-t.XXXXXX)"
	trap 'rm -f "${tmp_out:-}" "${tmp_err:-}"' EXIT

	local self="${BASH_SOURCE[0]}"
	run() { # <case> <env-assignments...> -> sets rc, fills tmp_out/tmp_err
		local c="$1"
		shift
		rc=0
		env "$@" bash "$self" --fixture "$c" >"$tmp_out" 2>"$tmp_err" || rc=$?
	}
	fail() {
		printf '  self-test FAILED: %s\n' "$1" >&2
		fails=$((fails + 1))
	}

	local m
	for m in 0 1; do
		local tag="ULTRAWIDELOCK_UI=$m"

		# 1. stdout carries the wrapped command's bytes and nothing else.
		run pass "ULTRAWIDELOCK_UI=$m" TERM=xterm
		[ "$rc" = 0 ] || fail "$tag: passing fixture exited $rc"
		[ "$(cat "$tmp_out")" = "$(printf 'a\nb\nc')" ] ||
			fail "$tag: stdout was not exactly the command output"
		# 2. ... and no escape sequence ever reaches it.
		if LC_ALL=C grep -q $'\033' "$tmp_out"; then
			fail "$tag: an escape sequence leaked onto stdout"
		fi
		# 3. The chrome names every step, on stderr.
		grep -q 'one' "$tmp_err" || fail "$tag: step label missing from stderr"

		# 4. A failing step propagates its own exit status, keeps its
		#    output, and does not swallow the steps before it.
		run fail "ULTRAWIDELOCK_UI=$m" TERM=xterm
		[ "$rc" = 3 ] || fail "$tag: failing step exited $rc, expected 3"
		grep -q 'boom' "$tmp_out" || fail "$tag: failing step lost its output"
		grep -q '^a$' "$tmp_out" || fail "$tag: earlier step's output lost"

		# 5. An unterminated final line is still delivered, once.
		run partial "ULTRAWIDELOCK_UI=$m" TERM=xterm
		[ "$(cat "$tmp_out")" = "$(printf 'x\ny-no-newline')" ] ||
			fail "$tag: unterminated last line mangled"

		# 6. Nothing is dropped or duplicated under volume.
		run volume "ULTRAWIDELOCK_UI=$m" TERM=xterm
		[ "$(wc -l <"$tmp_out" | tr -d ' ')" = 5000 ] ||
			fail "$tag: 5000 lines in, $(wc -l <"$tmp_out") out"
		[ "$(head -1 "$tmp_out")" = 1 ] && [ "$(tail -1 "$tmp_out")" = 5000 ] ||
			fail "$tag: line order or content changed"

		# 7. Arguments are passed through untouched.
		run args "ULTRAWIDELOCK_UI=$m" TERM=xterm
		[ "$(cat "$tmp_out")" = "$(printf '%s\n' 'a b' '  c  ' '$d' 'e*f')" ] ||
			fail "$tag: arguments were re-split or expanded"
	done

	# 8. Defaulted mode with stderr on a pipe must be plain: no escapes at all.
	#    ULTRAWIDELOCK_UI is cleared rather than left alone, so a developer who
	#    exports it does not turn this check green by accident.
	run pass "ULTRAWIDELOCK_UI=" TERM=xterm
	if LC_ALL=C grep -q $'\033' "$tmp_err"; then
		fail "no tty: escape sequences on stderr"
	fi
	# 9. TERM=dumb is plain even on a terminal.
	run pass "ULTRAWIDELOCK_UI=" TERM=dumb
	if LC_ALL=C grep -q $'\033' "$tmp_err"; then
		fail "TERM=dumb: escape sequences on stderr"
	fi
	# 10. NO_COLOR drops colour but keeps the run intact.
	run pass "ULTRAWIDELOCK_UI=1" TERM=xterm NO_COLOR=1
	[ "$rc" = 0 ] || fail "NO_COLOR: exited $rc"
	if LC_ALL=C grep -q $'\033\[3[0-9]m' "$tmp_err"; then
		fail "NO_COLOR: colour was still emitted"
	fi
	# 11. A C locale must not paint multi-byte glyphs.
	run pass "ULTRAWIDELOCK_UI=1" TERM=xterm LC_ALL=C LANG=C
	if LC_ALL=C grep -q $'\xe2' "$tmp_err"; then
		fail "C locale: UTF-8 glyphs painted anyway"
	fi
	# 12. A 20-column terminal still completes.
	run pass "ULTRAWIDELOCK_UI=1" TERM=xterm COLUMNS=20
	[ "$rc" = 0 ] || fail "20 columns: exited $rc"
	# 13. So does one that reports nonsense for its width.
	run pass "ULTRAWIDELOCK_UI=1" TERM=xterm COLUMNS=not-a-number
	[ "$rc" = 0 ] || fail "bad COLUMNS: exited $rc"
	# 14. An unwritable cache location costs accuracy, not the build.
	run pass "ULTRAWIDELOCK_UI=1" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT=/dev/null/nope
	[ "$rc" = 0 ] || fail "unwritable cache root: exited $rc"
	# 15. A ":<digits>" suffix is a weight hint and disappears from the label;
	#     any other colon belongs to the label and stays.
	run hints "ULTRAWIDELOCK_UI=0" TERM=xterm
	[ "$rc" = 0 ] || fail "weight hints: exited $rc"
	grep -q 'weighted$' "$tmp_err" || fail "weight hints: ':90' left in the label"
	grep -q 'cdk: live probe' "$tmp_err" || fail "weight hints: a real colon was eaten"

	# 16. A run that covers only some of the steps must leave the others'
	#     timings in the cache, not truncate the file to what it re-measured.
	local cacheroot cachefile
	cacheroot="$(mktemp -d -t ultrawidelock-ui-c.XXXXXX)"
	cachefile="$cacheroot/_ui/fixture.tsv"
	run both "ULTRAWIDELOCK_UI=0" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	run subset "ULTRAWIDELOCK_UI=0" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	if [ -r "$cachefile" ]; then
		grep -q '^alpha	' "$cachefile" ||
			fail "cache: a partial run dropped the step it did not re-run"
		grep -q '^beta	' "$cachefile" ||
			fail "cache: the step that did run was not recorded"
		[ "$(grep -c '^beta	' "$cachefile")" = 1 ] ||
			fail "cache: a re-measured step was written twice"
	else
		fail "cache: nothing was written to $cachefile"
	fi
	rm -rf "$cacheroot"

	# 17. A run that names no plan works on a cold cache -- counting steps, not
	#     percentages -- and picks up both the count and the weights from its own
	#     first run the second time round.
	cacheroot="$(mktemp -d -t ultrawidelock-ui-c.XXXXXX)"
	run unplanned "ULTRAWIDELOCK_UI=1" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	[ "$rc" = 0 ] || fail "no plan, cold cache: exited $rc"
	grep -q 'step 1' "$tmp_err" || fail "no plan, cold cache: no step counter"
	if grep -q '/0\]' "$tmp_err" || grep -q '0 steps' "$tmp_err"; then
		fail "no plan, cold cache: counted against a total it does not have"
	fi
	if LC_ALL=C grep -q '[0-9]%' "$tmp_err"; then
		fail "no plan, cold cache: invented a percentage with no total"
	fi
	run unplanned "ULTRAWIDELOCK_UI=1" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	[ "$rc" = 0 ] || fail "no plan, warm cache: exited $rc"
	grep -q '1/2' "$tmp_err" || fail "no plan, warm cache: the cache was not the plan"
	rm -rf "$cacheroot"

	# 18. The same two runs in plain mode, where the step counter is the whole
	#     display: "[1] alpha" cold, "[1/2] alpha" once the cache is the plan.
	cacheroot="$(mktemp -d -t ultrawidelock-ui-c.XXXXXX)"
	run unplanned "ULTRAWIDELOCK_UI=0" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	grep -q '\[1\] alpha' "$tmp_err" ||
		fail "no plan, plain cold: step lines were not bare counters"
	if grep -q '/0\]' "$tmp_err" || grep -q '0 steps' "$tmp_err"; then
		fail "no plan, plain cold: counted against a total it does not have"
	fi
	run unplanned "ULTRAWIDELOCK_UI=0" TERM=xterm ULTRAWIDELOCK_BUILD_ROOT="$cacheroot" \
		ULTRAWIDELOCK_UI_KEY=fixture
	grep -q '\[1/2\] alpha' "$tmp_err" ||
		fail "no plan, plain warm: the cache was not the plan"
	rm -rf "$cacheroot"

	if [ "$fails" -ne 0 ]; then
		printf '\n  ui: FAIL (%d checks failed)\n' "$fails" >&2
		return 1
	fi
	printf '  ui: PASS (18 checks - output fidelity, exit codes, degraded terminals)\n'
}

case "${BASH_SOURCE[0]}" in
"$0")
	set -euo pipefail
	case "${1:-}" in
	--self-test) _ui_self_test ;;
	--fixture)
		_ui_fixture "${2:-pass}"
		;;
	*)
		printf 'ui.sh is sourced by the long Make targets; see the header.\n' >&2
		printf 'Direct use: %s --self-test\n' "$0" >&2
		exit 2
		;;
	esac
	;;
esac
