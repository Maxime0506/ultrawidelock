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
#   woz_uwb/src/facade/{woz_bytes,woz_util}.h  portable shims that defer to the
#                                    Zephyr header under #ifdef __ZEPHYR__ and
#                                    carry their own fallback otherwise
#
# THE RATCHET. Every other exemption is a file still waiting on its unification
# tranche, listed in RATCHET below with the tranche that retires it. A ratchet
# entry that stops tripping the ban is a FAILURE ("stale") — finishing a
# conversion and shrinking this list are the same commit, so the list can only
# go down. When RATCHET is empty, modules/ is one-source and this header's
# permanent list is the whole story.

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
|modules/woz_uwb/src/facade/woz_bytes\.h
|modules/woz_uwb/src/facade/woz_util\.h)'
PERMANENT_RE=${PERMANENT_RE//$'\n'/}

# The ratchet: still-impure files and the tranche that retires each.
RATCHET=(
	modules/woz_nfc/src/pn532_bus_spi.c            # T4 move: Zephyr SPI driver -> ports/zephyr
	modules/woz_dfu/src/dfu_receiver.c             # T2c: convert to woz_osal + woz_flash
	modules/woz_dfu/src/dfu_applier.c              # T2c: convert to woz_flash
	modules/woz_dfu/src/dfu_smp_img.c              # T4 move: mcumgr/SMP glue -> ports/zephyr
	modules/woz_dw3000/platform/dw3000_hw.c        # T4a move: Zephyr GPIO backend -> ports/zephyr
	modules/woz_dw3000/platform/dw3000_spi.c       # T4a move: Zephyr SPI backend -> ports/zephyr
	modules/woz_dw3000/platform/dw3000_spi_trace.c # T4a move -> ports/zephyr
	modules/woz_uwb/src/facade/woz_logfmt.c        # T4 move: Zephyr log backend -> ports/zephyr
	modules/woz_uwb/src/facade/woz_logquiet.c      # T4 move: Zephyr log_ctrl -> ports/zephyr
	modules/woz_uwb/src/facade/woz_uwb_facade.c    # T4: split the SYS_INIT hfclk boost out
	modules/woz_uwb/src/shell/aliro_shell.c        # T4 move: Zephyr shell -> ports/zephyr
	modules/woz_anchor/src/woz_slam_lis2dh12.c     # T4 move: Zephyr I2C driver -> ports/zephyr
)

scan_paths() {
	git ls-files 'modules/*.c' 'modules/*.h' 'modules/*.cpp' 'modules/*.hpp'
}

in_ratchet() {
	local f needle="$1"
	for f in "${RATCHET[@]}"; do
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
	for f in "${RATCHET[@]}"; do
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
	scan
	;;
*)
	printf 'usage: %s [--self-test]\n' "$0" >&2
	exit 2
	;;
esac
