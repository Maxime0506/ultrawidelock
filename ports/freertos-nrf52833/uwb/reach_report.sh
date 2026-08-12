#!/usr/bin/env bash
#
# reach_report.sh — print what the UWB layer costs when it is reached.
#
# Called by the woz_uwb_reach target in uwb.cmake, which links the three ELFs
# this reads. See that file for why three links and what each one's roots mean.
#
#   reach_report.sh <size-tool> <baseline.elf> <facade.elf> <responder.elf> <responder.map>
#
# Two numbers matter and both are printed. The reachable total is what the layer
# adds to an image that calls it. The libwoz_uwb.a line under it is the part
# that is genuinely this port's to answer for: the rest is Mbed TLS, kernel and
# libgcc objects an image running BLE already links, so they are counted in the
# total and are not a marginal cost. Planning against the total overstates the
# layer by most of 40 KB.
set -euo pipefail

SIZE="$1" BASE="$2" FACADE="$3" RESP="$4" MAP="$5"

# text+data occupies flash; bss does not. Both are reported, and RAM is the
# column to read first: this part has 512 KB of flash against 128 KB of RAM, the
# Zephyr oracle carrying the same stack overflows RAM rather than flash, and
# OpenThread and the 802.15.4 receive pool are still outside the image. A layer
# that fits comfortably in flash can still be the one that costs the port its
# margin.
flash() { "$SIZE" "$1" | awk 'NR==2 {print $1 + $2}'; }
ram() { "$SIZE" "$1" | awk 'NR==2 {print $3}'; }

b=$(flash "$BASE")
f=$(flash "$FACADE")
r=$(flash "$RESP")
bm=$(ram "$BASE")
fm=$(ram "$FACADE")
rm_=$(ram "$RESP")

printf '\n  UWB reachable set (--gc-sections from named roots, not --whole-archive)\n\n'
printf '    %-30s %8s %9s %8s %9s\n' "roots" "flash" "over" "RAM" "over"
printf '    %-30s %8d %9s %8d %9s\n' "baseline, no UWB" "$b" "--" "$bm" "--"
printf '    %-30s %8d %9d %8d %9d\n' "responder facade" "$f" "$((f - b))" "$fm" "$((fm - bm))"
printf '    %-30s %8d %9d %8d %9d\n' "facade + Aliro ranging setup" "$r" "$((r - b))" "$rm_" \
	"$((rm_ - bm))"

# Attribute the responder link's flash bytes to the archive each input section
# came from. The map lists every kept section with its size and origin, so the
# per-archive sums say which layer owns the bytes. Sections are matched by the
# archive in parentheses; the linker's own wildcard aggregate lines carry no
# archive and are skipped, which is what keeps this from double counting.
printf '\n    where the reachable bytes live\n'
awk '
	# strtonum is a gawk extension and this has to run under the awk macOS
	# ships, so hex is converted a digit at a time.
	function hex(s,   i, c, n, d) {
		sub(/^0[xX]/, "", s)
		n = 0
		for (i = 1; i <= length(s); i++) {
			c = tolower(substr(s, i, 1))
			d = index("0123456789abcdef", c) - 1
			if (d < 0) return -1
			n = n * 16 + d
		}
		return n
	}
	# The map opens with "Discarded input sections", which lists exactly what
	# --gc-sections threw away. Counting those would report the archive as if
	# nothing had been collected, which is the whole-archive number again by a
	# longer route -- a link with no BLE root would show 60 KB of NimBLE.
	/^Linker script and memory map/ { live = 1; next }
	!live { next }
	# Long section names wrap onto the next line; rejoin before matching.
	/^ \.[^ ]+$/ { held = $0; next }
	held != "" { $0 = held " " $0; held = "" }
	{
		# .ARM.attributes is ELF metadata the loader never sees; counting it
		# adds several KB of flash that does not exist. .ARM.exidx does occupy
		# flash, so the prefix cannot simply be dropped.
		if ($1 ~ /^\.ARM\.attributes/) next
		if ($1 !~ /^\.(text|rodata|data|ARM|init|fini|glue)/) next
		if (NF < 4) next
		size = hex($3)
		if (size <= 0) next
		obj = $4
		if (obj !~ /\.a\(/) next
		arch = obj
		sub(/.*\//, "", arch)
		sub(/\(.*/, "", arch)
		bytes[arch] += size
	}
	END {
		for (a in bytes)
			if (bytes[a] >= 512)
				printf "      %8d  %s\n", bytes[a], a
	}
' "$MAP" | sort -rn

printf '\n    The libwoz_uwb.a line is the marginal cost of this layer. The rest is\n'
printf '    counted above but is already in any image that runs BLE.\n\n'
