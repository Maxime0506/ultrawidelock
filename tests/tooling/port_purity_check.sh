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
# and one shape is banned in the port trees, which is the same rule read from
# the other side: a port names exactly one OS (check_port_os). modules/ names
# none, ports/zephyr + firmware + anchor + ports/nrf5340dk name Zephyr,
# ports/esp32 names ESP-IDF. A file naming the wrong one is in the wrong tree.
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
#   woz_uwb/include/woz_util.h    portable shim that defers to the Zephyr
#                                    header under #ifdef __ZEPHYR__ and carries
#                                    its own fallback otherwise (woz_bytes.h,
#                                    its sibling, now lives in woz_port)
#
# THE RATCHET. Every other exemption is a file still waiting on its unification
# tranche, listed in RATCHET below with the tranche that retires it. A ratchet
# entry that stops tripping the ban is a FAILURE ("stale") — finishing a
# conversion and shrinking this list are the same commit, so the list can only
# go down. RATCHET is empty: modules/ is one-source and the permanent list
# above is the whole story.
#
# The permanent list is ratcheted the same way, because "permanent" is a claim
# about today's adapters, not a licence. Each named file must still trip the
# ban and each named directory must still exist; an adapter that becomes
# portable, or moves, fails the gate until its line goes too. An exemption
# nobody can retire is an exemption nobody is checking.
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
#   role manifests     modules/*/roles/*.list is the ONE place a shared source is
#                      assigned to a role; cmake/woz_roles.cmake and
#                      tests/host/sources.sh read them instead of carrying their
#                      own copies. check_manifests keeps that true: every listed
#                      path exists, no file sits in two roles (a consumer taking
#                      both would compile it twice), and every shared source
#                      under MANIFEST_ROOTS is in a role or in the
#                      NOT_MANIFESTED allowlist below with its reason.
#   public includes    no propagated CMake include path reaches modules/*/src
#   private headers    production modules, apps and ports never include a
#                      different module's private header; tests may white-box
#                      the implementation they compile

set -euo pipefail

# Same shape as uwb_seam_check.sh, the sibling gate this one mirrors.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
	R=$'\033[31m' G=$'\033[32m' Z=$'\033[0m'
else
	R='' G='' Z=''
fi

cd "$(dirname "$0")/../.."

# One definition each, used by the scan AND the self-test. The include shape is
# written once and specialised per OS, so the modules/ ban and the per-port ban
# can never drift apart on what an include looks like.
INC_RE='^[[:space:]]*#[[:space:]]*include[[:space:]]*["<]'
ZEPHYR_INC_RE="${INC_RE}zephyr/"
ESP_INC_RE="${INC_RE}(freertos/|esp_)"
INCLUDE_RE="${INC_RE}(zephyr/|freertos/|esp_)"
KERNEL_RE='(^|[^_[:alnum:]])(k_(work|sem|thread|timer|fifo|msleep|sleep|usleep|busy_wait|yield)|sys_reboot|flash_area_|SYS_INIT|K_(WORK|SEM|THREAD|TIMER|FIFO|MUTEX))'

# Prose naming a kernel symbol is not a call. Same filter as the seam gate:
# drop comment-opening lines from `grep -n` output, keep code with a trailing
# comment.
COMMENT_LINE_RE='^[0-9]+:[[:space:]]*(\*|//|/\*)'

# Permanent exemptions — see the header for why each is not a hole. Declared as
# plain paths, once, and compiled into the match regex below: the scan skips
# them and the staleness check re-proves each one is still needed, so the list
# cannot outlive its reasons.
PERMANENT_DIRS=(
	modules/woz_dw3000/dwt_uwb_driver   # vendored Qorvo decadriver
	modules/woz_dfu/src/detools         # vendored delta-patch engine
)
PERMANENT_FILES=(
	# The contract itself: these four headers name every platform on purpose,
	# because selecting the backend is what they are for. Listed one by one
	# rather than exempting modules/woz_port wholesale, so that a .c file
	# appearing beside them fails the gate instead of inheriting a blanket
	# pass. Every backend lives in a port tree now (ports/zephyr/osal,
	# ports/esp32/components/woz_port, tests/host/port). woz_flash.h is
	# deliberately absent: it is pure, and the ratchet says so if that changes.
	modules/woz_port/include/woz_bytes.h
	modules/woz_port/include/woz_log.h
	modules/woz_port/include/woz_osal.h
	modules/woz_port/include/woz_port.h
	modules/woz_aliro_stack/src/aliro_stack.cpp
	modules/woz_aliro_stack/src/session.cpp
	modules/woz_nfc/src/transport_pn532.cpp
	modules/woz_aliro_ecp/src/nfc_prop_ecp.cpp
	modules/woz_uwb/include/woz_util.h
)

permanent_re() { # the two lists as one anchored alternation, dots literal
	local p out=''
	for p in "${PERMANENT_DIRS[@]}"; do out="$out|${p//./\\.}/"; done
	for p in "${PERMANENT_FILES[@]}"; do out="$out|${p//./\\.}"; done
	printf '^(%s)' "${out#|}"
}
PERMANENT_RE=$(permanent_re)

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

	# Same discipline for the permanent list, which no tranche will ever empty:
	# an adapter that became pure, or moved, must lose its exemption in that
	# commit. Otherwise the entry keeps covering a path nobody is watching.
	for f in "${PERMANENT_FILES[@]}"; do
		if [ ! -f "$f" ]; then
			printf '%s  stale permanent exemption: %s (gone)%s\n' "$R" "$f" "$Z" >&2
			stale=$((stale + 1))
		elif [ -z "$(file_hits "$f")" ]; then
			printf '%s  stale permanent exemption: %s (now pure — drop it)%s\n' \
				"$R" "$f" "$Z" >&2
			stale=$((stale + 1))
		fi
	done
	for f in "${PERMANENT_DIRS[@]}"; do
		if [ ! -d "$f" ]; then
			printf '%s  stale permanent exemption: %s/ (gone)%s\n' "$R" "$f" "$Z" >&2
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
		[ "$stale" -eq 0 ] || printf '%scheck-purity: %d stale exemption(s) — an allowlist entry outlived its reason%s\n' \
			"$R" "$stale" "$Z" >&2
		return 1
	fi
	printf '%s  ok   modules/ is platform-pure outside woz_port and the exempt adapters%s\n' "$G" "$Z"
	printf '%s  ok   exemptions: %d permanent (%d dir, %d file), %d ratchet, none stale%s\n' \
		"$G" "$((${#PERMANENT_DIRS[@]} + ${#PERMANENT_FILES[@]}))" \
		"${#PERMANENT_DIRS[@]}" "${#PERMANENT_FILES[@]}" "${#RATCHET[@]}" "$Z"
	return 0
}

# ---- port trees keep to their own OS -----------------------------------------
#
# The other half of the one-source rule, and the half only reachable now that
# every port has a home: modules/ names no OS, and a port tree names exactly
# one. A Zephyr call in ports/esp32 (or an esp_/FreeRTOS call in ports/zephyr,
# firmware/, anchor/, ports/nrf5340dk/) is a file that landed in the wrong tree
# — it either belongs in the sibling port, or it is shared code that should sit
# in modules/ behind woz_port. Both readings mean the tree, not the file, is
# wrong, and neither is caught by the modules/ scan above.
#
# tests/ is deliberately absent: the host suites include fake <zephyr/*>
# headers on purpose, and their honesty is enforced by compiling, not by this.
ZEPHYR_TREES=(firmware anchor ports/zephyr ports/nrf5340dk)
ESP_TREES=(ports/esp32)

tree_sources() { # <dir>... -> the tracked C/C++ sources under them
	local d args=()
	for d in "$@"; do
		args+=("$d/*.c" "$d/*.h" "$d/*.cpp" "$d/*.hpp")
	done
	git ls-files "${args[@]}"
}

os_findings() { # <banned-re> <dir>... -> file:line:text per offence
	local re="$1" f
	shift
	while IFS= read -r f; do
		grep -nE "$re" "$f" | sed "s|^|$f:|" || true
	done < <(tree_sources "$@")
}

check_port_os() {
	local fails=0 n line
	n=$(tree_sources "${ZEPHYR_TREES[@]}" "${ESP_TREES[@]}" | wc -l | tr -d ' ')

	while IFS= read -r line; do
		[ -n "$line" ] || continue
		printf '%s  esp/freertos include in a Zephyr tree: %s%s\n' "$R" "$line" "$Z" >&2
		fails=$((fails + 1))
	done < <(os_findings "$ESP_INC_RE" "${ZEPHYR_TREES[@]}")

	while IFS= read -r line; do
		[ -n "$line" ] || continue
		printf '%s  zephyr include in the ESP-IDF tree: %s%s\n' "$R" "$line" "$Z" >&2
		fails=$((fails + 1))
	done < <(os_findings "$ZEPHYR_INC_RE" "${ESP_TREES[@]}")

	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d cross-OS include(s) in a port tree%s\n' "$R" "$fails" "$Z" >&2
		printf '  Move the file to the port whose OS it names, or into modules/\n' >&2
		printf '  behind woz_port if both ports need it.\n' >&2
		return 1
	fi
	printf '%s  ok   port trees: %d source(s), each naming only its own OS%s\n' "$G" "$n" "$Z"
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

# Propagated include directories from one CMake file. Only the public forms are
# emitted: Zephyr's global helper, PUBLIC/INTERFACE target scopes, and ESP-IDF
# INCLUDE_DIRS before PRIV_INCLUDE_DIRS. Variables resolve like cmake_path_list.
cmake_public_include_list() {
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
	function emit(s, p) {
		p = expand(s)
		if (p == "" || p ~ /[{}]/) return
		if (p !~ /^(\.\/|\/)/) p = dir "/" p
		print p
	}
	{
		line = $0
		sub(/#.*/, "", line)
		if (cmd == "" && match(line, /set\([A-Za-z_][A-Za-z0-9_]*[ \t]+[^)]+\)/)) {
			s = substr(line, RSTART + 4, RLENGTH - 5)
			name = s; sub(/[ \t].*/, "", name)
			val = s; sub(/^[^ \t]+[ \t]+/, "", val); gsub(/"/, "", val)
			v[name] = expand(val)
		}
		paren = line
		opens = gsub(/\(/, "(", paren)
		closes = gsub(/\)/, ")", paren)
		n = split(line, w, /[ \t()"]+/)
		for (i = 1; i <= n; i++) {
			t = w[i]
			if (t == "") continue
			if (cmd == "") {
				if (t == "zephyr_include_directories") { cmd = "zephyr"; pub = 1; continue }
				if (t == "target_include_directories") { cmd = "target"; pub = 0; continue }
				if (t == "idf_component_register") { cmd = "idf"; pub = 0; continue }
				continue
			}
			if (cmd == "target") {
				if (t == "PUBLIC" || t == "INTERFACE") { pub = 1; continue }
				if (t == "PRIVATE") { pub = 0; continue }
				if (t == "SYSTEM" || t == "BEFORE" || t == "AFTER") continue
			} else if (cmd == "idf") {
				if (t == "INCLUDE_DIRS") { pub = 1; continue }
				if (t ~ /^(SRCS|SRC_DIRS|EXCLUDE_SRCS|PRIV_INCLUDE_DIRS|REQUIRES|PRIV_REQUIRES|LDFRAGMENTS|EMBED_FILES|EMBED_TXTFILES|WHOLE_ARCHIVE)$/) {
					pub = 0
					continue
				}
			}
			if (pub) emit(t)
		}
		if (cmd != "") {
			depth += opens - closes
			if (depth <= 0) { cmd = ""; pub = 0; depth = 0 }
		}
	}' "$1"
}

cmake_files() { git ls-files '**/CMakeLists.txt'; }

check_public_includes() {
	local fails=0 n=0 f p canon repo
	repo=$(pwd -P)
	while IFS= read -r f; do
		while IFS= read -r p; do
			[ -n "$p" ] || continue
			n=$((n + 1))
			canon="$p"
			[ ! -d "$p" ] || canon=$(cd "$p" && pwd -P)
			case "$canon" in
			"$repo"/modules/woz_*/src | "$repo"/modules/woz_*/src/*)
				printf '%s  private include path is public: %s (named by %s)%s\n' \
					"$R" "$p" "$f" "$Z" >&2
				fails=$((fails + 1))
				;;
			esac
		done < <(cmake_public_include_list "$f")
	done < <(cmake_files)
	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d modules/*/src include path(s) propagate to consumers%s\n' \
			"$R" "$fails" "$Z" >&2
		return 1
	fi
	printf '%s  ok   public includes: %d propagated path(s), none enters modules/*/src%s\n' \
		"$G" "$n" "$Z"
}

# Owned private headers only. The vendored trees are dependencies with their
# own layout and do not define this repository's module boundary.
private_headers() {
	git ls-files 'modules/woz_*/src/*.h' 'modules/woz_*/src/**/*.h' \
		| grep -vE '^modules/woz_dfu/src/detools/'
}

# Production C/C++ files. Tests may include private headers to white-box the
# implementation units they compile; app and port code may not.
boundary_sources() {
	git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' 'modules/*.hpp' \
		'firmware/*.c' 'firmware/*.h' 'firmware/*.cpp' 'firmware/*.hpp' \
		'anchor/*.c' 'anchor/*.h' 'anchor/*.cpp' 'anchor/*.hpp' \
		'ports/*.c' 'ports/*.h' 'ports/*.cpp' 'ports/*.hpp' \
		| grep -vE '^(modules/woz_dw3000/dwt_uwb_driver/|modules/woz_dfu/src/detools/|ports/esp32/test/)'
}

# Print the private header an include crosses into, or print nothing. A module's
# own implementation may use its own src headers. Its public headers may not.
private_header_target() { # <source> <include-token> <private-header>...
	local src="$1" inc="$2" src_owner='' public_header=0 own=0 candidate=''
	local h owner rel matched
	shift 2
	case "$src" in
	modules/woz_*/*)
		src_owner=${src#modules/}
		src_owner=${src_owner%%/*}
		case "$src" in modules/"$src_owner"/include/*) public_header=1 ;; esac
		;;
	esac
	for h in "$@"; do
		owner=${h#modules/}
		owner=${owner%%/*}
		rel=${h#modules/$owner/src/}
		matched=0
		case "$inc" in
		*/*)
			case "$inc" in "$rel" | src/"$rel" | */src/"$rel") matched=1 ;; esac
			;;
		*) [ "$inc" != "${h##*/}" ] || matched=1 ;;
		esac
		[ "$matched" -eq 1 ] || continue
		if [ "$owner" = "$src_owner" ] && [ "$public_header" -eq 0 ]; then
			own=1
		else
			[ -n "$candidate" ] || candidate="$h"
		fi
	done
	[ "$own" -eq 0 ] || return 1
	[ -n "$candidate" ] || return 1
	printf '%s\n' "$candidate"
}

check_private_headers() {
	local private=() h f hit inc target fails=0 n=0
	while IFS= read -r h; do private+=("$h"); done < <(private_headers)
	while IFS= read -r f; do
		while IFS= read -r hit; do
			[ -n "$hit" ] || continue
			n=$((n + 1))
			inc=$(printf '%s\n' "$hit" | sed -E 's/^[0-9]+:[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]+)[>"].*/\1/')
			target=$(private_header_target "$f" "$inc" "${private[@]}") || continue
			printf '%s  private header include: %s:%s -> %s%s\n' \
				"$R" "$f" "${hit%%:*}" "$target" "$Z" >&2
			fails=$((fails + 1))
		done < <(grep -nE "${INC_RE}[^>\"]+[>\"]" "$f" || true)
	done < <(boundary_sources)
	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d production include(s) cross a module private boundary%s\n' \
			"$R" "$fails" "$Z" >&2
		return 1
	fi
	printf '%s  ok   private headers: %d production include(s) stay within module boundaries%s\n' \
		"$G" "$n" "$Z"
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
	ports/esp32/test/on_target_ec/main/CMakeLists.txt
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

# ---- role manifests ----------------------------------------------------------
#
# Trees whose every tracked .c must be assigned to a role. Deliberately not the
# whole of modules/: the vendored decadriver is reached BY manifests (core.list
# points into it) but is not ours to enumerate, and single-port modules
# (woz_matter, woz_ml, woz_anchor, ...) have one consumer each, so a manifest
# would be a second copy of a list that exists once.
MANIFEST_ROOTS=(
	modules/woz_aliro/src
	modules/woz_uwb/src
)

# Shared sources deliberately left out of every role, with the reason. Same
# ratchet discipline as RATCHET: an entry that becomes manifested, or stops
# existing, is a FAILURE — the allowlist can only shrink deliberately.
#
#   aliro_stepup.c      the step-up worker's engine half; only the ESP reader
#                       component compiles it, and its worker body is gated to
#                       empty without CONFIG_WOZ_ALIRO_STEPUP
#   aliro_assert_ec.c   the P-256 half of the assert pair — the only one with a
#                       crypto dependency, so it cannot join wire_codecs
#   uwb_rxdiag.c        Zephyr-module only: the ESP port omits it and stubs the
#                       two decadriver seams it would otherwise supply
#   uwb_selftest.c      Zephyr-module only, CONFIG_WOZ_UWB_SELFTEST, default n
NOT_MANIFESTED=(
	modules/woz_aliro/src/aliro_assert_ec.c
	modules/woz_aliro/src/aliro_stepup.c
	modules/woz_uwb/src/driver/uwb_rxdiag.c
	modules/woz_uwb/src/driver/uwb_selftest.c
)

manifest_files() { git ls-files 'modules/*/roles/*.list'; }

# One manifest -> its repo-relative paths, comments and blank lines dropped.
# The same three rules cmake/woz_roles.cmake and tests/host/sources.sh apply.
manifest_paths() {
	sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' "$@"
}

check_manifests() {
	local fails=0 n=0 lists=0 f p dup

	while IFS= read -r f; do
		lists=$((lists + 1))
		while IFS= read -r p; do
			n=$((n + 1))
			if [ ! -f "$p" ]; then
				printf '%s  missing manifest path: %s (listed by %s)%s\n' \
					"$R" "$p" "$f" "$Z" >&2
				fails=$((fails + 1))
			fi
		done < <(manifest_paths "$f")
	done < <(manifest_files)

	# A file in two roles is compiled twice by any consumer taking both.
	while IFS= read -r dup; do
		[ -n "$dup" ] || continue
		printf '%s  source in two roles: %s%s\n' "$R" "$dup" "$Z" >&2
		fails=$((fails + 1))
		# shellcheck disable=SC2046 # tracked manifest paths, no whitespace
	done < <(manifest_paths $(manifest_files) | LC_ALL=C sort | uniq -d)

	if [ "$fails" -gt 0 ]; then
		printf '%scheck-purity: %d broken role manifest entr(ies)%s\n' "$R" "$fails" "$Z" >&2
		return 1
	fi
	printf '%s  ok   role manifests: %d list(s), %d path(s), all resolve, none doubled%s\n' \
		"$G" "$lists" "$n" "$Z"

	# Coverage: every shared source is in a role or on the allowlist.
	local manifested allow src stale=0
	# shellcheck disable=SC2046 # tracked manifest paths, no whitespace
	manifested=$(manifest_paths $(manifest_files) | LC_ALL=C sort -u)
	allow=$(printf '%s\n' "${NOT_MANIFESTED[@]}" | LC_ALL=C sort -u)
	while IFS= read -r src; do
		[ -n "$src" ] || continue
		printf '%s\n' "$manifested" | grep -qxF "$src" && continue
		printf '%s\n' "$allow" | grep -qxF "$src" && continue
		printf '%s  unmanifested shared source: %s%s\n' "$R" "$src" "$Z" >&2
		fails=$((fails + 1))
	done < <(git ls-files "${MANIFEST_ROOTS[@]/%//*.c}")

	for src in "${NOT_MANIFESTED[@]}"; do
		if [ ! -f "$src" ] || printf '%s\n' "$manifested" | grep -qxF "$src"; then
			printf '%s  stale NOT_MANIFESTED entry: %s%s\n' "$R" "$src" "$Z" >&2
			stale=$((stale + 1))
		fi
	done

	if [ "$fails" -gt 0 ] || [ "$stale" -gt 0 ]; then
		[ "$fails" -eq 0 ] || printf '%scheck-purity: %d shared source(s) in no role — add to a roles/*.list or to NOT_MANIFESTED with a reason%s\n' \
			"$R" "$fails" "$Z" >&2
		[ "$stale" -eq 0 ] || printf '%scheck-purity: %d stale NOT_MANIFESTED entr(ies) — shrink the list%s\n' \
			"$R" "$stale" "$Z" >&2
		return 1
	fi
	printf '%s  ok   role coverage: every shared source is in a role, %d allowlisted%s\n' \
		"$G" "${#NOT_MANIFESTED[@]}" "$Z"
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

	# Per-OS halves: each must fire on the other OS and stay quiet on its own,
	# or check_port_os would ban a port tree from the platform it is written for.
	local zline='#include <zephyr/kernel.h>' eline='#include "freertos/task.h"'
	printf '%s\n' "$zline" | grep -qE "$ZEPHYR_INC_RE" ||
		{ printf '%s  self-test FAILED: zephyr half missed its own include%s\n' "$R" "$Z" >&2
			fails=$((fails + 1)); }
	printf '%s\n' "$eline" | grep -qE "$ESP_INC_RE" ||
		{ printf '%s  self-test FAILED: esp half missed its own include%s\n' "$R" "$Z" >&2
			fails=$((fails + 1)); }
	if printf '%s\n' "$zline" | grep -qE "$ESP_INC_RE"; then
		printf '%s  self-test FAILED: esp half fired on a zephyr include%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	if printf '%s\n' "$eline" | grep -qE "$ZEPHYR_INC_RE"; then
		printf '%s  self-test FAILED: zephyr half fired on an esp include%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	# The tree lists must be disjoint, and must not reach into modules/ or
	# tests/ — a tree in both lists could name neither OS and pass.
	local zt et
	for zt in "${ZEPHYR_TREES[@]}"; do
		for et in "${ESP_TREES[@]}"; do
			case "$zt" in "$et" | "$et"/*) ;; *) continue ;; esac
			printf '%s  self-test FAILED: %s is in both OS tree lists%s\n' "$R" "$zt" "$Z" >&2
			fails=$((fails + 1))
		done
	done
	for zt in "${ZEPHYR_TREES[@]}" "${ESP_TREES[@]}"; do
		case "$zt" in
		modules | modules/* | tests | tests/*)
			printf '%s  self-test FAILED: %s is not a port tree%s\n' "$R" "$zt" "$Z" >&2
			fails=$((fails + 1))
			;;
		esac
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: per-OS halves and tree lists are exact%s\n' "$G" "$Z"

	# Exemption exactness: a prefix that swallowed a portable file would silence
	# the gate without anyone noticing.
	local f
	for f in modules/woz_matter/src/matter_tlv.c modules/woz_aliro/src/aliro_reader.c \
		modules/woz_uwb/src/ccc/ccc_shim.c modules/woz_dw3000/src/deca_port.c; do
		if [[ $f =~ $PERMANENT_RE ]] || in_ratchet "$f"; then
			printf '%s  self-test FAILED: %s is exempt, but it must stay pure%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: exemptions cover only the declared adapters%s\n' "$G" "$Z"

	# ...and every declared exemption is still earning it: the staleness rule
	# the scan applies, re-run here so --self-test alone names the offender.
	for f in "${PERMANENT_FILES[@]}"; do
		if [ ! -f "$f" ]; then
			printf '%s  self-test FAILED: permanent exemption %s is gone%s\n' "$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		elif [ -z "$(file_hits "$f")" ]; then
			printf '%s  self-test FAILED: permanent exemption %s is pure — drop it%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	for f in "${PERMANENT_DIRS[@]}"; do
		if [ ! -d "$f" ]; then
			printf '%s  self-test FAILED: permanent exemption %s/ is gone%s\n' "$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	# The compiled regex must match exactly what the two lists declare — a
	# quoting slip there would silently widen or void the exemption set.
	for f in "${PERMANENT_FILES[@]}" "${PERMANENT_DIRS[@]/%//x.c}"; do
		if ! [[ $f =~ $PERMANENT_RE ]]; then
			printf '%s  self-test FAILED: declared exemption %s does not match the compiled regex%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: every declared exemption still trips the ban%s\n' "$G" "$Z"

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

	# Public include parser: all three propagated CMake forms fire, while their
	# private counterparts stay quiet.
	cat >"$fixdir/public.cmake" <<-'EOF'
		set(PUB ${REPO_ROOT}/modules/public/src)
		zephyr_include_directories(${PUB})
		zephyr_library_include_directories(${REPO_ROOT}/modules/library_private/src)
		target_include_directories(app PUBLIC ${REPO_ROOT}/modules/target/src
			PRIVATE ${REPO_ROOT}/modules/target_private/src)
		idf_component_register(
			SRCS x.c
			INCLUDE_DIRS ${REPO_ROOT}/modules/idf/src
			PRIV_INCLUDE_DIRS ${REPO_ROOT}/modules/idf_private/src)
	EOF
	out=$(cmake_public_include_list "$fixdir/public.cmake")
	if [ "$out" != "$(printf './modules/public/src\n./modules/target/src\n./modules/idf/src')" ]; then
		printf '%s  self-test FAILED: public include parser lost scope%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	[ "$fails" -ne 0 ] || printf '%s  self-test: public include scopes expose API paths only%s\n' "$G" "$Z"

	# Private-header matcher: an external source and a module API header must
	# trip, while a module implementation may include its own private sibling.
	local private_fix=(modules/woz_alpha/src/secret.h modules/woz_alpha/src/protocol/wire.h)
	if [ "$(private_header_target modules/woz_beta/src/use.c secret.h "${private_fix[@]}")" != \
		"modules/woz_alpha/src/secret.h" ]; then
		printf '%s  self-test FAILED: private header matcher missed a cross-module include%s\n' \
			"$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	if [ "$(private_header_target ports/zephyr/use.c protocol/wire.h "${private_fix[@]}")" != \
		"modules/woz_alpha/src/protocol/wire.h" ]; then
		printf '%s  self-test FAILED: private header matcher missed a qualified include%s\n' \
			"$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	if [ "$(private_header_target modules/woz_alpha/include/api.h secret.h "${private_fix[@]}")" != \
		"modules/woz_alpha/src/secret.h" ]; then
		printf '%s  self-test FAILED: private header matcher missed a public-header leak%s\n' \
			"$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	if private_header_target modules/woz_alpha/src/impl.c secret.h "${private_fix[@]}" >/dev/null; then
		printf '%s  self-test FAILED: private header matcher rejected an owned sibling%s\n' \
			"$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	[ "$fails" -ne 0 ] || printf '%s  self-test: private header ownership distinguishes API, implementation and consumers%s\n' "$G" "$Z"

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

	# Manifest parser: comments (whole-line and trailing), blank lines and
	# stray whitespace vanish; nothing else does. Must agree with
	# cmake/woz_roles.cmake and tests/host/sources.sh, which parse the same
	# files with different tools.
	printf '%s\n' '# a role manifest' '' 'modules/x/src/a.c' \
		'  modules/x/src/b.c  ' 'modules/x/src/c.c # why' '#modules/x/src/never.c' \
		>"$fixdir/fix.list"
	if [ "$(manifest_paths "$fixdir/fix.list")" != "$(printf 'modules/x/src/a.c\nmodules/x/src/b.c\nmodules/x/src/c.c')" ]; then
		printf '%s  self-test FAILED: manifest parser disagrees on the fixture%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	# ...and a doubled path must be visible to the duplicate detector.
	if [ "$(manifest_paths "$fixdir/fix.list" "$fixdir/fix.list" | LC_ALL=C sort | uniq -d | wc -l)" -ne 3 ]; then
		printf '%s  self-test FAILED: duplicate detector missed a doubled path%s\n' "$R" "$Z" >&2
		fails=$((fails + 1))
	fi
	# The allowlist is exact: a file that IS in a role must not be swallowed.
	for f in modules/woz_aliro/src/aliro_tlv.c modules/woz_uwb/src/ccc/ccc_kdf.c; do
		if printf '%s\n' "${NOT_MANIFESTED[@]}" | grep -qxF "$f"; then
			printf '%s  self-test FAILED: %s is allowlisted but lives in a role%s\n' \
				"$R" "$f" "$Z" >&2
			fails=$((fails + 1))
		fi
	done
	[ "$fails" -ne 0 ] || printf '%s  self-test: manifest parser and allowlist are exact%s\n' "$G" "$Z"
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
	check_port_os || rc=1
	check_public_includes || rc=1
	check_private_headers || rc=1
	check_manifests || rc=1
	check_build_paths || rc=1
	check_patch_symbols || rc=1
	exit "$rc"
	;;
*)
	printf 'usage: %s [--self-test]\n' "$0" >&2
	exit 2
	;;
esac
