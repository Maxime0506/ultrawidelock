#!/usr/bin/env bash
#
# toolchain.sh — what the host suites need, whether this host has it, how to get it.
#
# The host suites skip loudly when a tool is absent. Skipping loudly is honest,
# but it leaves the reader to work out what to install, from where, and at which
# version. That is this script: one manifest, two modes.
#
#   scripts/toolchain.sh            report every tool, its suite, and its status
#   scripts/toolchain.sh install    install the missing ones, after confirming
#
# Nothing is installed without being printed first and agreed to. `install`
# shows the exact command list and waits for a y; -y answers it in advance for
# unattended use.
#
# Versions matter for some of these, whose output changes between releases.
# Those rows carry a pin and say so when the host disagrees.
#
# Out of scope: the firmware toolchains. NCS (~6.5 GB, `make bootstrap`) and
# ESP-IDF are per-target installs with their own procedures. This covers the
# host suites only.
#
# Env:
#   ASSUME_YES=1   same as `install -y`
#   NO_COLOR=1     plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

# $PY: the interpreter the python-side suites run under, which is the repo-local
# .venv when it exists. Defined in one place so this script probes exactly the
# interpreter the runners will use, not merely the one on PATH.
# shellcheck disable=SC1091  # repo-local, sourced for $PY
. "$ROOT/tests/host/sources.sh"

MODE=check
ASSUME_YES="${ASSUME_YES:-}"
for arg in "$@"; do
	case "$arg" in
	check | install) MODE="$arg" ;;
	-y | --yes) ASSUME_YES=1 ;;
	-h | --help)
		sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		echo "toolchain.sh: unknown argument '$arg' (try: check | install | -y)" >&2
		exit 2
		;;
	esac
done

# ---- glyphs + colour --------------------------------------------------------
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
	BOLD=$'\033[1m' DIM=$'\033[2m' CYAN=$'\033[36m' GRN=$'\033[32m'
	RED=$'\033[31m' YEL=$'\033[33m' RESET=$'\033[0m'
	CHK="✓" CRS="✗" TIL="~" DOT="•"
else
	BOLD="" DIM="" CYAN="" GRN="" RED="" YEL="" RESET=""
	CHK="+" CRS="x" TIL="~" DOT="*"
fi
HR="------------------------------------------------------------------------"

# ---- host ------------------------------------------------------------------
OS="$(uname -s)"
ARCH="$(uname -m)"

# The package manager to phrase system installs in. Homebrew wins where it
# exists (including Linuxbrew) because it is the one that needs no sudo.
PM=""
SUDO=""
if command -v brew >/dev/null 2>&1; then
	PM=brew
elif command -v apt-get >/dev/null 2>&1; then
	PM=apt SUDO="sudo "
elif command -v dnf >/dev/null 2>&1; then
	PM=dnf SUDO="sudo "
elif command -v pacman >/dev/null 2>&1; then
	PM=pacman SUDO="sudo "
elif command -v zypper >/dev/null 2>&1; then
	PM=zypper SUDO="sudo "
fi

# Python-packaged tools go through pipx (isolated venv per tool, so a pinned
# clang-format cannot collide with anything else) and fall back to a --user pip
# install. Bare `pip install` is not offered: on a modern distro or Homebrew
# python it is refused by PEP 668, and working around that belongs to the user.
#
# The two python *packages* take neither route: they go into the repo-local
# .venv, which sidesteps PEP 668 entirely rather than negotiating with it, and
# which the runners resolve on their own. See tool_install below.

# --force because these commands are only ever emitted for a tool that is
# missing or sitting on the wrong version, and plain `pipx install` declines to
# touch a package it has already installed — which is exactly the repin case.
pipx_or_pip() { # $1 = pip spec, e.g. clang-format==22.1.8
	if command -v pipx >/dev/null 2>&1; then
		printf "pipx install --force '%s'" "$1"
	else
		printf "python3 -m pip install --user '%s'" "$1"
	fi
}

# ---- manifest --------------------------------------------------------------
# One row per tool. bash-3.2 case functions rather than associative arrays:
# macOS ships bash 3.2.
#
# Ordered as the host suites run, then the two python imports no suite checks
# for — see tool_gate("markdown") for why they are here.
TOOLS=(
	cc python3 llvm-cov cbmc markdown coverage
)

# Bench tools, not gates. nrfutil installs the NCS toolchain, tio owns live
# serial sessions for the TUI and `make term`, probe-rs is the only thing that
# reads the DWM3001CDK's RTT console (`make cdk-rtt`), and mcumgr is the only
# thing that speaks MCUboot's serial recovery (`make dfu`), so `make bootstrap`
# offers all four. They are reported here because "what does this machine
# still need" is the question this script answers.
#
# It is kept in its own list because it must NEVER set the exit status. Someone
# who only runs the host suites has every tool they need without it. The
# row is reported, and `install` offers it; nothing here fails without it.
FW_TOOLS=(tio nrfutil probe-rs mcumgr)

# Which suite stops working without it. This is the "why do I need this" column,
# and it is the reason a row exists at all.
tool_gate() {
	case "$1" in
	cc) echo "test, test-san" ;;
	python3) echo "test, coverage" ;;
	llvm-cov) echo "coverage" ;;
	cbmc) echo "cbmc" ;;
	# No suite names these two, which is exactly the trap. Without them
	# coverage.sh reports no python rows — a weaker measurement, still green.
	markdown) echo "test, coverage (silently weaker)" ;;
	coverage) echo "coverage (python rows)" ;;
	tio) echo "TUI live serial / make term" ;;
	nrfutil) echo "make bootstrap / build / flash" ;;
	probe-rs) echo "make cdk-rtt (CDK console)" ;;
	mcumgr) echo "make dfu (CDK serial recovery)" ;;
	esac
}

# The pinned host-tool versions. Empty = any version is fine.
tool_pin() {
	case "$1" in
	markdown) echo "3.8" ;;
	*) echo "" ;;
	esac
}

# The tools whose pin MUST resolve. If one of these comes back empty the table
# above lost a row, which would otherwise degrade silently into "no pin, any
# version is fine".
PINNED="markdown"

# Present on this host? Echoes the version (or a bare "installed") and returns
# 0; returns 1 when absent. Two rows are not a plain `command -v`:
#   llvm-cov  macOS keeps it inside the Xcode SDK, reachable only via xcrun —
#             which is how tests/host/coverage.sh calls it.
#   markdown  a python import, not a binary.
tool_probe() {
	local v
	case "$1" in
	cc)
		command -v cc >/dev/null 2>&1 || return 1
		cc --version 2>&1 | head -1
		;;
	python3)
		command -v python3 >/dev/null 2>&1 || return 1
		python3 --version 2>&1 | head -1
		;;
	llvm-cov)
		if command -v llvm-cov >/dev/null 2>&1; then
			v="$(llvm-cov --version 2>&1 | grep -i version | head -1)"
		elif command -v xcrun >/dev/null 2>&1 && xcrun --find llvm-cov >/dev/null 2>&1; then
			v="$(xcrun llvm-cov --version 2>&1 | grep -i version | head -1) (via xcrun)"
		else
			return 1
		fi
		echo "$v"
		;;
	mcumgr)
		# A cobra CLI with no --version flag: it is a SUBCOMMAND. The generic
		# probe below gets "Error: unknown flag: --version" and prints that in
		# the version column, which reads as a broken tool rather than a
		# working one. Upstream builds report "0.0.0-dev" with no pin anywhere,
		# so any answer here is only ever "it runs".
		command -v mcumgr >/dev/null 2>&1 || return 1
		mcumgr version 2>&1 | head -1
		;;
	markdown)
		"$PY" -c 'import markdown; print("markdown " + markdown.__version__)' 2>/dev/null || return 1
		;;
	coverage)
		"$PY" -c 'import coverage; print("coverage " + coverage.__version__)' 2>/dev/null || return 1
		;;
	*)
		command -v "$1" >/dev/null 2>&1 || return 1
		"$1" --version </dev/null 2>&1 | head -1
		;;
	esac
}

# The command that installs it here. Empty = this host has no packaged route and
# the row prints a pointer instead.
tool_install() {
	case "$1" in
	cc)
		case "$OS" in
		Darwin) echo "xcode-select --install" ;;
		*) case "$PM" in
			apt) echo "${SUDO}apt-get install -y clang" ;;
			dnf) echo "${SUDO}dnf install -y clang" ;;
			pacman) echo "${SUDO}pacman -S --needed clang" ;;
			zypper) echo "${SUDO}zypper install -y clang" ;;
			brew) echo "brew install llvm" ;;
			*) echo "" ;;
			esac ;;
		esac
		;;
	python3)
		case "$PM" in
		brew) echo "brew install python" ;;
		apt) echo "${SUDO}apt-get install -y python3 python3-pip" ;;
		dnf) echo "${SUDO}dnf install -y python3 python3-pip" ;;
		pacman) echo "${SUDO}pacman -S --needed python python-pip" ;;
		zypper) echo "${SUDO}zypper install -y python3 python3-pip" ;;
		*) echo "" ;;
		esac
		;;
	llvm-cov)
		# macOS reaches llvm-cov/llvm-profdata through xcrun, so the install is
		# the Command Line Tools, not a package.
		case "$OS" in
		Darwin) echo "xcode-select --install" ;;
		*) case "$PM" in
			apt) echo "${SUDO}apt-get install -y llvm" ;;
			dnf) echo "${SUDO}dnf install -y llvm" ;;
			pacman) echo "${SUDO}pacman -S --needed llvm" ;;
			zypper) echo "${SUDO}zypper install -y llvm" ;;
			brew) echo "brew install llvm" ;;
			*) echo "" ;;
			esac ;;
		esac
		;;
	cbmc)
		case "$PM" in
		brew) echo "brew install cbmc" ;;
		apt) echo "${SUDO}apt-get install -y cbmc" ;;
		*) echo "" ;; # no first-party package elsewhere; see tool_note
		esac
		;;
	# Imported by the suites themselves, so these go into python3's own
	# environment, not an isolated pipx venv: a pipx venv is invisible to
	# `import markdown` in a test, which is the whole point of installing them.
	#
	# Which is also why an externally-managed python has no automatic route
	# here. The two ways out both belong to the user, not to this script: run
	# the gates from a venv, or override the refusal and accept the risk. Both
	# are spelled out under the missing rows.
	# One command for both, because one venv serves both and running it twice
	# is just a no-op the second time. The runners find .venv on their own (see
	# $PY in tests/host/sources.sh), so there is nothing to activate and
	# nothing lands outside the checkout.
	markdown | coverage)
		echo "python3 -m venv .venv && .venv/bin/pip install --quiet --upgrade pip 'markdown==$(tool_pin markdown)' coverage"
		;;
	tio)
		case "$PM" in
		brew) echo "brew install tio" ;;
		apt) echo "${SUDO}apt-get install -y tio" ;;
		dnf) echo "${SUDO}dnf install -y tio" ;;
		pacman) echo "${SUDO}pacman -S --needed tio" ;;
		zypper) echo "${SUDO}zypper install -y tio" ;;
		*) echo "" ;;
		esac
		;;
	# Nordic ships it as a signed binary from their own site, and only Homebrew
	# packages it. Every other host gets the note instead of a wrong command:
	# the distro repos do not carry it, so guessing one would fail loudly on a
	# tool nobody needs unless they are building firmware.
	nrfutil)
		case "$PM" in
		brew) echo "brew install nrfutil" ;;
		*) echo "" ;;
		esac
		;;
	# The project's own tap is the packaged route; `brew info probe-rs` resolves
	# to probe-rs/probe-rs, not homebrew/core. The alternative everywhere else is
	# `cargo install probe-rs-tools`, a multi-minute source build that is not
	# offered here: this row never blocks a gate, and a toolchain script should
	# not start a compile on someone's behalf.
	probe-rs)
		case "$PM" in
		brew) echo "brew install probe-rs/probe-rs/probe-rs" ;;
		*) echo "" ;;
		esac
		;;
	# Ignores $PM on purpose: no distro or Homebrew formula packages this, and
	# upstream ships it only as a Go module. So the availability question is
	# "is there a go", not "which package manager". GOBIN decides where it lands,
	# which on a version-managed Go is inside that version's directory -- a place
	# the next `go` upgrade deletes. Worth pointing somewhere stable.
	mcumgr)
		if command -v go >/dev/null 2>&1; then
			echo "go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest"
		else
			echo ""
		fi
		;;
	esac
}

# Printed under a row that has no install command on this host.
tool_note() {
	case "$1" in
	cbmc) echo "no package for this host — releases: https://github.com/diffblue/cbmc/releases" ;;
	nrfutil) echo "firmware only — https://www.nordicsemi.com/Products/Development-tools/nrf-util" ;;
	probe-rs) echo "bench only — https://probe.rs/docs/getting-started/installation/" ;;
	mcumgr) echo "bench only — needs a go toolchain: https://github.com/apache/mynewt-mcumgr-cli" ;;
	*) echo "install it however this host prefers, then re-run" ;;
	esac
}

# Pull the leading dotted number out of a --version line, for the pin compare.
version_of() { echo "$1" | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1; }

# ---- drift check: every gate tool verify.sh names must have a row here -----
# verify.sh's gate_need() and gate_need_py() are the authority on what the gates
# need. Reading them rather than restating them means a new gate cannot quietly
# arrive without an install route: this check fails until someone adds the row.
#
# Both functions, not just the first. gate_need_py() arrived later and covering
# only gate_need() would have left python dependencies drifting freely, which is
# the exact hole this check exists to close.
verify_needs() { # <function name> -> the names its case arms echo
	awk -v fn="$1" '
		$0 == fn "() {" { inside = 1; next }
		inside && /^}/  { inside = 0 }
		inside          { print }
	' "$ROOT/scripts/verify.sh" | sed -n 's/.*) echo "\([^"]*\)" ;;.*/\1/p'
}

missing_rows=""
unparsed=""
if [ -r "$ROOT/scripts/verify.sh" ]; then
	for fn in gate_need gate_need_py; do
		names="$(verify_needs "$fn")"
		# An empty result is not "nothing is needed", it is "the parse stopped
		# working" — a reworded case arm, a renamed function. Left silent, the
		# check would pass by knowing nothing at all, which is worse than
		# failing.
		if [ -z "$(printf '%s' "$names" | tr -d '[:space:]')" ]; then
			unparsed="${unparsed:+$unparsed }$fn"
			continue
		fi
		for need in $names; do
			case " ${TOOLS[*]} " in
			*" $need "*) ;;
			*) missing_rows="${missing_rows:+$missing_rows }$need" ;;
			esac
		done
	done
fi

# Second half of the same idea: a pin that no longer resolves. An empty pin
# reads as "nothing is pinned", which is a lie that costs someone a failure on
# a version they were never told about.
stale_pins=""
for t in $PINNED; do
	[ -n "$(tool_pin "$t")" ] || stale_pins="${stale_pins:+$stale_pins }$t"
done

# ---- report ----------------------------------------------------------------
printf '\n  %s%sopenaliro toolchain%s  %s%s  the host tools the suites need%s\n\n' \
	"$BOLD" "$CYAN" "$RESET" "$DIM" "$DOT" "$RESET"
printf '  %shost: %s %s  %s  package manager: %s%s\n\n' \
	"$DIM" "$OS" "$ARCH" "$DOT" "${PM:-none detected}" "$RESET"

printf '  %s%-14s %-33s %s%s\n' "$BOLD" "Tool" "Suite" "Status" "$RESET"
printf '  %s%s%s\n' "$DIM" "$HR" "$RESET"

NEEDED=()
nok=0 nmiss=0 nold=0
for t in "${TOOLS[@]}"; do
	pin="$(tool_pin "$t")"
	if got="$(tool_probe "$t")"; then
		have="$(version_of "$got")"
		if [ -n "$pin" ] && [ -n "$have" ] && [ "$have" != "$pin" ]; then
			# Off the pin counts as something to fix, not as present: the
			# install route below repins it, and only the pinned tools can
			# land here, so none of these is a harmless minor-version drift.
			nold=$((nold + 1))
			NEEDED+=("$t")
			printf '  %s%s%s %-12s %s%-33s%s%s (pinned %s)%s\n' \
				"$YEL" "$TIL" "$RESET" "$t" "$DIM" "$(tool_gate "$t")" "$YEL" "$have" "$pin" "$RESET"
		else
			nok=$((nok + 1))
			printf '  %s%s%s %-12s %s%-33s%s%s\n' \
				"$GRN" "$CHK" "$RESET" "$t" "$DIM" "$(tool_gate "$t")" "$RESET" "$got"
		fi
	else
		nmiss=$((nmiss + 1))
		NEEDED+=("$t")
		printf '  %s%s%s %-12s %s%-33s%sMISSING%s%s\n' \
			"$RED" "$CRS" "$RESET" "$t" "$DIM" "$(tool_gate "$t")" "$RED" \
			"${pin:+ (pinned $pin)}" "$RESET"
	fi
done
printf '  %s%s%s\n' "$DIM" "$HR" "$RESET"
printf '  %s%d tools %s %d present %s %d missing %s %d off the pin%s\n\n' \
	"$DIM" "${#TOOLS[@]}" "$DOT" "$nok" "$DOT" "$nmiss" "$DOT" "$nold" "$RESET"

# Bench rows, below the line and outside every count above. A missing one is a
# fact about what this machine can build or connect to, not a gap in what it can
# verify, so it is stated and then left alone.
nfw=0
for t in "${FW_TOOLS[@]}"; do
	if got="$(tool_probe "$t")"; then
		printf '  %s%s%s %-12s %s%-33s%s%s\n' \
			"$GRN" "$CHK" "$RESET" "$t" "$DIM" "$(tool_gate "$t")" "$RESET" "$got"
	else
		nfw=$((nfw + 1))
		NEEDED+=("$t")
		printf '  %s%s%s %-12s %s%-33s%snot installed · optional bench tool%s\n' \
			"$YEL" "$TIL" "$RESET" "$t" "$DIM" "$(tool_gate "$t")" "$YEL" "$RESET"
	fi
done
printf '\n'

if [ "$nold" -gt 0 ]; then
	printf '  %s%s A version off the pin can behave differently in either direction.%s\n' \
		"$YEL" "$TIL" "$RESET"
	printf '  %s  Re-run with `install` to repin.%s\n\n' \
		"$YEL" "$RESET"
fi

if [ -n "$missing_rows" ]; then
	printf '  %s%s verify.sh needs a tool this manifest does not describe: %s%s\n' \
		"$RED" "$CRS" "$missing_rows" "$RESET"
	printf '  %sAdd it to TOOLS + tool_gate/tool_probe/tool_install in this file.%s\n\n' \
		"$DIM" "$RESET"
fi

if [ -n "$unparsed" ]; then
	printf '  %s%s could not read %s out of scripts/verify.sh%s\n' \
		"$RED" "$CRS" "$unparsed" "$RESET"
	printf '  %sThe drift check is blind until verify_needs() matches it again.%s\n\n' \
		"$DIM" "$RESET"
fi

if [ -n "$stale_pins" ]; then
	printf '  %s%s no pinned version for %s%s\n' \
		"$RED" "$CRS" "$stale_pins" "$RESET"
	printf '  %sAdd it to tool_pin() — an unresolved pin silently stops checking.%s\n\n' \
		"$DIM" "$RESET"
fi

# ---- what to run -----------------------------------------------------------
if [ "$nmiss" -eq 0 ] && [ "$nold" -eq 0 ]; then
	{ [ -n "$missing_rows" ] || [ -n "$stale_pins" ] || [ -n "$unparsed" ]; } && exit 1
	printf '  %s%s every host gate has its tool. `make verify` will skip nothing.%s\n\n' \
		"$GRN" "$CHK" "$RESET"
	# Every gate is covered, so this is a success whatever the firmware row
	# says. `install` still goes on to offer it; `check` has said its piece.
	[ "$MODE" = install ] && [ "$nfw" -gt 0 ] || exit 0
fi

CMDS=()
STUCK=()
for t in "${NEEDED[@]}"; do
	c="$(tool_install "$t")"
	if [ -z "$c" ]; then
		STUCK+=("$t")
		continue
	fi
	# markdown and coverage share one venv command; emitting it twice would
	# just re-run it. Any future pair that collapses this way is covered too.
	case " ${CMDS[*]:-} " in
	*" $c "*) continue ;;
	esac
	CMDS+=("$c")
done

if [ "${#STUCK[@]}" -gt 0 ]; then
	printf '  %sNo packaged route on this host for:%s\n' "$BOLD" "$RESET"
	for t in "${STUCK[@]}"; do
		printf '    %s%-12s%s %s\n' "$YEL" "$t" "$RESET" "$(tool_note "$t")"
	done
	printf '\n'
fi

if [ "${#CMDS[@]}" -eq 0 ]; then
	printf '  %s%s nothing here can be installed automatically.%s\n\n' "$YEL" "$TIL" "$RESET"
	exit 1
fi

printf '  %sTo install or repin %d tool(s):%s\n\n' "$BOLD" "${#CMDS[@]}" "$RESET"
for c in "${CMDS[@]}"; do printf '    %s\n' "$c"; done
printf '\n'

if [ "$MODE" != install ]; then
	printf '  %sRun them yourself, or let this script do it: %smake tools-install%s\n\n' \
		"$DIM" "$BOLD" "$RESET"
	exit 1
fi

# ---- install ---------------------------------------------------------------
# The command list above is the whole of what runs. Nothing is added here, and
# nothing runs before the answer.
if [ -z "$ASSUME_YES" ]; then
	if [ ! -t 0 ]; then
		printf '  %s%s not a terminal and no -y — nothing installed.%s\n\n' "$YEL" "$TIL" "$RESET"
		exit 1
	fi
	printf '  %sRun these %d command(s) now? [y/N] %s' "$BOLD" "${#CMDS[@]}" "$RESET"
	read -r reply
	case "$reply" in
	y | Y | yes | YES) ;;
	*)
		printf '\n  %snothing installed.%s\n\n' "$DIM" "$RESET"
		exit 1
		;;
	esac
fi

printf '\n'
nfail=0
for c in "${CMDS[@]}"; do
	printf '  %s%s%s %s\n' "$CYAN" "$DOT" "$RESET" "$c"
	if bash -c "$c"; then
		printf '  %s%s%s done\n\n' "$GRN" "$CHK" "$RESET"
	else
		nfail=$((nfail + 1))
		printf '  %s%s%s FAILED (continuing)\n\n' "$RED" "$CRS" "$RESET"
	fi
done

# A pipx or --user install lands in ~/.local/bin, which is not on every PATH.
if ! echo ":$PATH:" | grep -q ":$HOME/.local/bin:"; then
	printf '  %s%s $HOME/.local/bin is not on PATH — pipx and --user installs land there.%s\n\n' \
		"$YEL" "$TIL" "$RESET"
fi

if [ "$nfail" -gt 0 ]; then
	printf '  %s%s %d command(s) failed. Re-run `make tools` to see what is still missing.%s\n\n' \
		"$RED" "$CRS" "$nfail" "$RESET"
	exit 1
fi
printf '  %s%s installed. Re-run `make tools` to confirm, then `make verify`.%s\n\n' \
	"$GRN" "$CHK" "$RESET"
