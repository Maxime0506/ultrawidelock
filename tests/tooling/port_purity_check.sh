#!/usr/bin/env bash
#
# port_purity_check.sh — keep modules/ compilable from one source on every port.
#
# WHAT IS BEING PREVENTED. modules/ is the shared tree: the same files compile
# under Zephyr, ESP-IDF and the host cc. A platform include or a kernel call
# added to a shared file builds cleanly on the port it was written on and breaks
# the other two at the worst time — after the change looked done. This gate
# fails the commit instead. Two shapes are banned in modules/** sources:
#
#   1. platform includes   #include <zephyr/...> / "freertos/..." / <esp_...>
#   2. Zephyr kernel API   k_work*, k_sem*, k_thread*, k_timer*, k_fifo*,
#                          k_msleep, SYS_INIT, K_WORK_*, K_SEM_*, flash_area_*,
#                          sys_reboot — platform code goes through woz_port
#
#   tests/tooling/port_purity_check.sh              # scan the tracked sources
#   tests/tooling/port_purity_check.sh --self-test  # prove the gate can fail
#   make check / make purity                        # runs it as the `purity` suite
#
# Exit 0 clean, 1 on a finding, 2 if the gate could not do its job.
#
# WHAT IS EXEMPT, permanently, and why each one is not a hole:
#
#   modules/woz_port/                the contract itself: its whole job is to be
#                                    the one place platform branches live
#   modules/woz_dw3000/dwt_uwb_driver/   vendored Qorvo decadriver
#   modules/woz_dfu/src/detools/         vendored delta-patch engine
#   woz_aliro_stack/{aliro_stack,session}.cpp  adapters to the Nordic add-on's
#                                    <aliro/*> API; the add-on's own headers
#                                    include Zephyr, so these can never be pure
#   woz_nfc/src/transport_pn532.cpp  same class of adapter: its threading
#                                    contract IS the add-on's workqueue
#                                    (AliroWorkqueueSubmit takes a k_work), and
#                                    it includes aliro/ + reader_storage headers
#   woz_aliro_ecp/src/nfc_prop_ecp.cpp   same: grafts into the add-on's
#                                    subsys/nfc_prop, add-on headers included
#   woz_uwb/src/facade/woz_util.h    portable shim that defers to the Zephyr
#                                    header under #ifdef __ZEPHYR__ and carries
#                                    its own fallback otherwise (woz_bytes.h,
#                                    its sibling, now lives in woz_port)
#
# THE RATCHET. Every other exemption is a file still waiting on its unification
# tranche, listed in RATCHET below with the tranche that retires it. A ratchet
# entry that stops tripping the ban is a FAILURE ("stale") — finishing a
# conversion and shrinking this list are the same commit, so the list can only
# go down. When RATCHET is empty, modules/ is one-source and this header's
# permanent list is the whole story.
#
# TWO MORE PERMANENT CHECKS ride in this gate because no Zephyr/ESP build runs
# on this machine — a path or symbol a build file hardcodes is otherwise proven
# only on hardware CI, after the tree already moved:
#
#   build-file paths   every path literal a CMakeLists or a -DZEPHYR_EXTRA_MODULES
#                      list names must exist in the tree (check_build_paths)
#   patch symbols      every woz identifier a ports/nrf5340dk/patches/*.patch
#                      grafts into the Nordic add-on must still be defined in
#                      modules/ or ports/ (check_patch_symbols)

set -euo pipefail

# Same shape as uwb_seam_check.sh, the sibling gate this one mirrors.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	R=$'\033[31m' G=$'\033[32m' Z=$'\033[0m'
else
	R='' G='' Z=''
fi

cd "$(dirname "$0")/../.."

# One definition each, used by the scan AND the self-test.
INCLUDE_RE='^[[:space:]]*#[[:space:]]*include[[:space:]]*["<](zephyr/|freertos/|esp_)'
KERNEL_RE='(^|[^_[:alnum:]])(k_(work|sem|thread|timer|fifo|msleep|sleep|usleep|busy_wait|yield)|sys_reboot|flash_area_|SYS_INIT|K_(WORK|SEM|THREAD|TIMER|FIFO|MUTEX))'

# Prose naming a kernel symbol is not a call. Same filter as the seam gate:
# drop comment-opening lines from `grep -n` output, keep code with a trailing
# comment.
COMMENT_LINE_RE='^[0-9]+:[[:space:]]*(\*|//|/\*)'

# Permanent exemptions — see the header for why each is not a hole.
PERMANENT_RE='^(modules/woz_port/
|modules/woz_dw3000/dwt_uwb_driver/
|modules/woz_dfu/src/detools/
|modules/woz_aliro_stack/src/aliro_stack\.cpp
|modules/woz_aliro_stack/src/session\.cpp
|modules/woz_nfc/src/transport_pn532\.cpp
|modules/woz_aliro_ecp/src/nfc_prop_ecp\.cpp
|modules/woz_uwb/src/facade/woz_util\.h)'
PERMANENT_RE=${PERMANENT_RE//$'\n'/}

# The ratchet: still-impure files and the tranche that retires each. EMPTY
# since T4: modules/ is one-source and the permanent list above is the whole
# story. Stays declared so a regression has somewhere honest to land — with a
# tranche tag and a reason — and so the ok-line keeps reporting the count.
RATCHET=()

scan_paths() {
	git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' 'modules/*.hpp'
}

in_ratchet() {
	# ${arr[@]+...}: bash 3.2 + set -u treats an empty array expansion as unbound.
	local f needle="$1"
	for f in ${RATCHET[@]+"${RATCHET[@]}"}; do
		[ "$f" = "$needle" ] && return 0
	done
	return 1
}

# Banned hits in one file, comment lines dropped, line numbers kept.
file_hits() {
	{
		grep -nE "$INCLUDE_RE" "$1" || true
		grep -nE "$KERNEL_RE" "$1" | grep -vE "$COMMENT_LINE_RE" || true
	} | sort -t: -k1,1n -u
}

# ---- scan -------------------------------------------------------------------

scan() {
	local findings=0 stale=0 f hits

	while IFS= read -r f; do
		[[ $f =~ $PERMANENT_RE ]] && continue
		if in_ratchet "$f"; then
			continue
		fi
		hits=$(file_hits "$f")
		[ -n "$hits" ] || continue
		while IFS= read -r hit; do
			printf '%s  %s:%s%s\n' "$R" "$f" "$hit" "$Z"
			findings=$((findings + 1))
		done <<<"$hits"
	done < <(scan_paths)

	# A ratchet entry that no longer trips the ban is finished work that forgot
	# to shrink the list — or a moved file leaving a hole. Either way: fail.
	for f in ${RATCHET[@]+"${RATCHET[@]}"}; do
		if [ ! -f "$f" ] || [ -z "$(file_hits "$f")" ]; then
			printf '%s  stale ratchet entry: %s%s\n' "$R" "$f" "$Z" >&2
			stale=$((stale + 1))
		fi
	done

	if [ "$findings" -gt 0 ] || [ "$stale" -gt 0 ]; then
		if [ "$findings" -gt 0 ]; then
			printf '%scheck-purity: %d platform reference(s) in shared modules/%s\n' \
				"$R" "$findings" "$Z" >&2
			printf '  Go through woz_port (woz_port.h / woz_log.h), or move the file\n' >&2
			printf '  to a port tree. RATCHET additions need a tranche tag and a reason.\n' >&2
		fi
		[ "$stale" -eq 0 ] || printf '%scheck-purity: %d stale RATCHET entr(ies) — shrink the list%s\n' \
			"$R" "$stale" "$Z" >&2
		return 1
	fi
	printf '%s  ok   modules/ is platform-pure outside woz_port and the exempt adapters%s\n' "$G" "$Z"
	printf '%s  ok   conversion ratchet: %d file(s) remain, none stale%s\n' "$G" "${#RATCHET[@]}" "$Z"
	return 0
}

# ---- build-file path literals -----------------------------------------------
#
# Resolved path candidates of one CMake file, one per line. Variables expand
# from the file's own single-line set() lines plus REPO_ROOT and
# CMAKE_CURRENT_{SOURCE,LIST}_DIR; a word still carrying ${...} or $ENV{...}
# after that is outside this gate's reach and is skipped, as are comments and
# if(EXISTS ...) probes (existence there is the condition, not a promise).
# Two shapes survive: anything a resolved variable anchored ("./..."), and a
# bare relative file with a source-ish extension (src/main.c, platform/x.c).
cmake_path_list() {
	local d
	d=$(dirname "$1")
	case $d in /*) ;; *) d="./$d" ;; esac
	awk -v dir="$d" '
	function expand(s,  k, hit) {
		gsub(/\$\{REPO_ROOT\}/, ".", s)
		gsub(/\$\{CMAKE_CURRENT_SOURCE_DIR\}/, dir, s)
		gsub(/\$\{CMAKE_CURRENT_LIST_DIR\}/, dir, s)
		do {
			hit = 0
			for (k in v)
				if (index(s, "${" k "}")) { gsub("\\$\\{" k "\\}", v[k], s); hit = 1 }
		} while (hit)
		return s
	}
	{
		sub(/#.*/, "")
		if ($0 ~ /EXISTS/) next
		# message() strings are prose for humans, not path promises; skip the
		# whole call, tracking parens so multi-line FATAL_ERROR blocks vanish.
		if (inmsg) {
			inmsg += gsub(/\(/, "(") - gsub(/\)/, ")")
			if (inmsg < 0) inmsg = 0
			next
		}
		if (match($0, /message[ \t]*\(/)) {
			rest = substr($0, RSTART)
			inmsg = gsub(/\(/, "(", rest) - gsub(/\)/, ")", rest)
			if (inmsg < 0) inmsg = 0
			$0 = substr($0, 1, RSTART - 1)
		}
		if (match($0, /set\([A-Za-z_][A-Za-z0-9_]*[ \t]+[^)]+\)/)) {
			s = substr($0, RSTART + 4, RLENGTH - 5)
			name = s; sub(/[ \t].*/, "", name)
			val = s; sub(/^[^ \t]+[ \t]+/, "", val); gsub(/"/, "", val)
			v[name] = expand(val)
		}
		n = split($0, w, /[ \t()"]+/)
		for (i = 1; i <= n; i++) {
			p = expand(w[i])
			if (p ~ /[{}]/ || p !~ /\//) continue
			if (p ~ /^(\.\/|\/)/) { print p; continue }
			if (p ~ /^[A-Za-z0-9_][A-Za-z0-9_.-]*(\/[A-Za-z0-9_.-]+)+\.(c|cc|cpp|h|hpp|conf|overlay|yml|cmake)$/)
				print dir "/" p
		}
	}' "$1"
}

# Every -DZEPHYR_EXTRA_MODULES / -DEXTRA_ZEPHYR_MODULES entry the build
# recipes pass, repo-relative. Covers scripts/build-nrf5340dk.sh and mk/*.mk,
# notably mk/cdk.mk injecting woz_dfu at the sysbuild level.
module_list_paths() {
	grep -hoE -- '-D(ZEPHYR_EXTRA_MODULES|EXTRA_ZEPHYR_MODULES)=[^[:space:]]+' \
		scripts/build-nrf5340dk.sh mk/*.mk \
		| sed -e 's/^-D[A-Z_]*=//' -e "s/[\"']//g" \
		| tr ';' '\n' \
		| sed -e 's|^\$TREE|.|' -e 's|^\$(REPO_ROOT)|.|' -e 's|^\${REPO_ROOT}|.|'
}

# The build files whose hardcoded paths this gate resolves.
BUILD_FILES=(
	firmware/CMakeLists.txt
	anchor/CMakeLists.txt
	ports/nrf5340dk/initiator/CMakeLists.txt
	ports/nrf5340dk/on_target_ec/CMakeLists.txt
	ports/zephyr/CMakeLists.txt
	modules/*/CMakeLists.txt
	ports/esp32/components/*/CMakeLists.txt
)

check_build_paths() {
	local fails=0 n=0 f p
	for f in "${BUILD_FILES[@]}"; do
		while IFS= read -r p; do
			n=$((n + 1))
			if [ ! -e "$p" ]; then
				printf '%s  missing path: %s (named by %s)%s\n' "$R" "$p" "$f" "$Z" >&2
				fails=$((fails + 1))
			fi
		done < <(cmake_path_list "$f")
	done
	while IFS= read -r p; do
		n=$((n + 1))
		if [ ! -d "$p" ]; then
			printf '%s  missing module dir: %s (-D*ZEPHYR*_MODULES in scripts/ or mk/)%s\n' \
				"$R" "$p" "$Z" >&2
			fails=$((fails + 1))
		fi
	done < <(module_list_paths)
	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d dangling build-file path(s) — the Zephyr/ESP builds cannot prove them here%s\n' \
			"$R" "$fails" "$Z" >&2
		return 1
	fi
	printf '%s  ok   build-file paths: %d literal(s) resolve in the tree%s\n' "$G" "$n" "$Z"
}

# ---- patch-symbol tripwire ---------------------------------------------------
#
# Identifier shapes a Nordic-add-on patch grafts in. A rename in modules/ or
# ports/ leaves the patch applying cleanly and breaks only at add-on build
# time, on hardware CI. Fail here instead.
PATCH_SYM_RE='woz_[a-z0-9_]+|WozNfc::[A-Za-z]+|CONFIG_WOZ_[A-Z0-9_]+'
# Names a patch itself coins rather than references (never defined in-tree).
PATCH_LOCAL_RE='^woz_uwb_impl$' # LOG_MODULE name local to custom_impl-uwb.patch

patch_syms() { # <patch> -> unique woz identifiers on its + lines
	grep -E '^\+' "$1" | grep -oE "$PATCH_SYM_RE" | LC_ALL=C sort -u
}

patch_sym_defined() { # <sym> -> 0 if modules/ or ports/ still carries it
	local m hits
	case "$1" in
	WozNfc::*)
		# In-tree the methods live inside `namespace WozNfc { ... }`, so the
		# qualified spelling never appears; require one file naming both.
		m="${1#WozNfc::}"
		hits=$(git grep -lF 'WozNfc' -- modules ports ':!ports/nrf5340dk/patches' 2>/dev/null) || return 1
		[ -n "$hits" ] || return 1
		# shellcheck disable=SC2086 # tracked paths, no whitespace
		grep -qlE "(^|[^[:alnum:]_])${m}([^[:alnum:]_]|\$)" $hits
		;;
	*)
		git grep -qF "$1" -- modules ports ':!ports/nrf5340dk/patches' 2>/dev/null
		;;
	esac
}

check_patch_symbols() {
	local fails=0 n=0 p sym
	for p in ports/nrf5340dk/patches/*.patch; do
		while IFS= read -r sym; do
			[ -n "$sym" ] || continue
			[[ $sym =~ $PATCH_LOCAL_RE ]] && continue
			n=$((n + 1))
			if ! patch_sym_defined "$sym"; then
				printf '%s  dangling patch symbol: %s (grafted by %s)%s\n' \
					"$R" "$sym" "$p" "$Z" >&2
				fails=$((fails + 1))
			fi
		done < <(patch_syms "$p")
	done
	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d patch symbol(s) no longer defined in modules/ or ports/%s\n' \
			"$R" "$fails" "$Z" >&2
		return 1
	fi
	printf '%s  ok   add-on patches: %d woz symbol(s) still defined in the tree%s\n' "$G" "$n" "$Z"
}

# ---- self-test --------------------------------------------------------------
#
# Plant each shape the scan must catch and each it must ignore; fail loudly on
# either. Then prove the exemptions are exact.

self_test() {
	local fails=0 line n=0 quiet=0

	local should_fire=(
		'#include <zephyr/kernel.h>'
		'  #include "freertos/FreeRTOS.h"'
		'#include <esp_timer.h>'
		'	k_work_submit(&ctx.work);'
		'	if (k_sem_take(&s, K_MSEC(50)) != 0) {'
		'	rc = flash_area_open(FIXED_PARTITION_ID(slot1), &fa);'
		'SYS_INIT(boost, PRE_KERNEL_1, 0);'
		'K_WORK_DELAYABLE_DEFINE(rearm, rearm_fn);'
		'	sys_reboot(SYS_REBOOT_COLD);'
	)
	local should_not=(
		'	woz_work_submit(&ctx.work);'
		'	woz_sem_take(&s, 50);'
		'#include "woz_port.h"'
		'#include <aliro_reader.h>'
		'	int task_sem = mask_semantics(x);'
		'	stack_free(p);'
		'#define ESP_NOTE 1 /* not an include */'
	)

	for line in "${should_fire[@]}"; do
		if printf '%s\n' "$line" | grep -qE "$INCLUDE_RE|$KERNEL_RE"; then
			n=$((n + 1))
		else
			printf '%s  self-test FAILED: missed: %s%s\n' "$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: detector fires on all %d impure shapes%s\n' "$G" "$n" "$Z"

	for line in "${should_not[@]}"; do
		if printf '%s\n' "$line" | grep -qE "$INCLUDE_RE|$KERNEL_RE"; then
			printf '%s  self-test FAILED: fired on a legitimate line: %s%s\n' "$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		else
			quiet=$((quiet + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: quiet on all %d legitimate shapes%s\n' "$G" "$quiet" "$Z"

	# Comment filter: drops prose, keeps code with a trailing comment.
	local drop=('12: * uses k_work_reschedule under the hood' '7://	k_msleep(5);')
	local keep='20:	woz_sem_give(&s); /* not k_sem_give */'
	for line in "${drop[@]}"; do
		if ! printf '%s\n' "$line" | grep -qE "$COMMENT_LINE_RE"; then
			printf '%s  self-test FAILED: comment filter kept: %s%s\n' "$R" "$line" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	if printf '%s\n' "$keep" | grep -qE "$COMMENT_LINE_RE"; then
		printf '%s  self-test FAILED: comment filter dropped a line of code%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	[ "$fails" -ne 0 ] || printf '%s  self-test: comment filter drops prose, keeps code%s\n' "$G" "$Z"

	# Exemption exactness: a prefix that swallowed a portable file would silence
	# the gate without anyone noticing.
	local f
	for f in modules/woz_matter/src/matter_tlv.c modules/woz_aliro/src/aliro_reader.c \
		modules/woz_uwb/src/ccc/ccc_shim.c modules/woz_dw3000/platform/deca_port.c; do
		if [[ $f =~ $PERMANENT_RE ]] || in_ratchet "$f"; then
			printf '%s  self-test FAILED: %s is exempt, but it must stay pure%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: exemptions cover only the declared adapters%s\n' "$G" "$Z"

	# Path extractor: resolves set() variables and REPO_ROOT, joins relative
	# sources, and stays quiet on comments, EXISTS probes, unresolved ${...}
	# and prose like "and/or".
	local fixdir out
	fixdir=$(mktemp -d -t purity-selftest.XXXXXX)
	mkdir "$fixdir/src" # the set() value itself is emitted and must resolve
	cat >"$fixdir/fix.cmake" <<-'EOF'
		set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
		target_sources(app PRIVATE ${SRC}/nope.c src/also.c ${REPO_ROOT}/modules/ghost/gone.c)
		# comment/only.c
		if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/maybe/absent.h")
		zephyr_include_directories(${UNDEFINED_VAR}/x and/or)
	EOF
	out=$(cmake_path_list "$fixdir/fix.cmake")
	local want
	for want in "$fixdir/src/nope.c" "$fixdir/src/also.c" "./modules/ghost/gone.c"; do
		if ! printf '%s\n' "$out" | grep -qxF "$want"; then
			printf '%s  self-test FAILED: path extractor missed: %s%s\n' "$R" "$want" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	local nowant
	for nowant in only.c absent.h UNDEFINED_VAR and/or; do
		if printf '%s\n' "$out" | grep -qF "$nowant"; then
			printf '%s  self-test FAILED: path extractor emitted: %s%s\n' "$R" "$nowant" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	# ...and the scan half must flag every emitted-but-absent path.
	local missing=0 pth
	while IFS= read -r pth; do
		[ -e "$pth" ] || missing=$((missing + 1))
	done <<<"$out"
	if [ "$missing" -ne 3 ]; then
		printf '%s  self-test FAILED: expected 3 missing fixture paths, saw %d%s\n' \
			"$R" "$missing" "$Z" >&2
		fails=$((fails + 1))
	fi
	[ "$fails" -ne 0 ] || printf '%s  self-test: path extractor resolves, joins and filters correctly%s\n' "$G" "$Z"

	# Patch tripwire: + lines only, all three identifier shapes, and the
	# resolver must fail on an invented symbol while passing real ones.
	cat >"$fixdir/fix.patch" <<-'EOF'
		--- a/x.cpp
		+++ b/x.cpp
		+	woz_phantom_symbol_xyz();
		+	WozNfc::Init();
		+	if (CONFIG_WOZ_ALIRO) {}
		-	woz_minus_line_only();
	EOF
	if [ "$(patch_syms "$fixdir/fix.patch")" != "$(printf 'CONFIG_WOZ_ALIRO\nWozNfc::Init\nwoz_phantom_symbol_xyz')" ]; then
		printf '%s  self-test FAILED: patch_syms extraction wrong for the fixture%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	if patch_sym_defined woz_phantom_symbol_xyz; then
		printf '%s  self-test FAILED: tripwire resolved an invented symbol%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	for pth in CONFIG_WOZ_ALIRO WozNfc::Init; do
		if ! patch_sym_defined "$pth"; then
			printf '%s  self-test FAILED: tripwire lost a real symbol: %s%s\n' "$R" "$pth" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: patch tripwire fires on invented symbols only%s\n' "$G" "$Z"
	rm -rf "$fixdir"

	if [ "$fails" -ne 0 ]; then
		printf '%scheck-purity: the gate itself is broken%s\n' "$R" "$Z" >&2
		return 2
	fi
	return 0
}

case "${1-}" in
--self-test)
	self_test
	;;
"")
	rc=0
	scan || rc=1
	check_build_paths || rc=1
	check_patch_symbols || rc=1
	exit "$rc"
	;;
*)
	printf 'usage: %s [--self-test]\n' "$0" >&2
	exit 2
	;;
esac
