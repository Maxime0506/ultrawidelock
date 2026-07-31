#!/usr/bin/env bash
#
# security-fw.sh — the shipped artifact, which every other gate in this repo reasons about only
# indirectly.
#
# semgrep, clang-tidy, CodeQL and CBMC all read source. The thing a user actually flashes is
# build/merged.hex, and between the source and that file sit a linker, a Kconfig tree, a
# generated device tree, a vendor blob and whatever `west build` decided to bake in. Nothing here
# has ever looked at the result. That gap is where a build-host path leak, a test key that
# survived a #ifdef, or a payload appended after the link would live, and none of those are
# visible to a source scanner by construction.
#
#   scripts/security-fw.sh                       # every check, on build/merged.hex
#   scripts/security-fw.sh --image out/x.bin     # explicit artifact
#   scripts/security-fw.sh strings               # one: keys strings size dwarf
#   make security-fw
#
# Exit 0 clean, 1 on a finding, 2 if there is no artifact to examine.
#
# Intel HEX is parsed here rather than shelled out to objcopy. objcopy is not on a mac by default
# and arm-none-eabi-objcopy lives inside the NCS toolchain, so requiring either turns "the gate
# ran" into "the gate ran if you had bootstrapped", which is the soft-skip this repo's gates are
# written to refuse. The parser below is thirty lines and has no dependencies.
#
# Env:
#   FW_IMAGE=path                artifact (default: build/merged.hex, then build/zephyr/merged.hex)
#   FW_DENYLIST=path             byte patterns that must not ship (default: security/fw-denylist.txt)
#   FW_SIZE_BASELINE=path        recorded sizes (default: security/fw-size-baseline.txt)
#   FW_SIZE_WARN=2 FW_SIZE_FAIL=10   growth percentages
#   FW_UPDATE_BASELINE=1         rewrite the size record instead of comparing
#   NO_COLOR=1                   plain output
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

DENYLIST="${FW_DENYLIST:-security/fw-denylist.txt}"
BASELINE="${FW_SIZE_BASELINE:-security/fw-size-baseline.txt}"
SIZE_WARN="${FW_SIZE_WARN:-2}"
SIZE_FAIL="${FW_SIZE_FAIL:-10}"

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

# ---- locate the artifact ---------------------------------------------------
IMAGE="${FW_IMAGE:-}"
while [ "$#" -gt 0 ]; do
	case "$1" in
	--image)
		IMAGE="$2"
		shift 2
		;;
	*) break ;;
	esac
done
if [ -z "$IMAGE" ]; then
	for cand in build/merged.hex build/zephyr/merged.hex build/zephyr/zephyr.hex; do
		[ -f "$cand" ] && {
			IMAGE="$cand"
			break
		}
	done
fi
if [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; then
	printf '\n  %s%s%s no firmware image to examine (looked for build/merged.hex).\n' \
		"$YEL" "$WRN" "$RESET"
	printf '      Run `make build`, or pass --image <path>. This gate does not pass without\n'
	printf '      an artifact: "nothing to check" and "checked, clean" are different answers.\n\n'
	exit 2
fi

# ---- hex -> raw bytes ------------------------------------------------------
RAW="$ROOT/build/fw-audit/image.bin"
mkdir -p "$(dirname "$RAW")"

case "$IMAGE" in
*.hex)
	FW_IN="$IMAGE" FW_OUT="$RAW" python3 - <<'PY' || exit 1
import os, sys

# Intel HEX: :LLAAAATT<data><cksum>. 00 data, 01 EOF, 02/04 segment/linear base.
src, dst = os.environ["FW_IN"], os.environ["FW_OUT"]
chunks, base = [], 0
with open(src) as fh:
    for lineno, ln in enumerate(fh, 1):
        ln = ln.strip()
        if not ln.startswith(":"):
            continue
        try:
            b = bytes.fromhex(ln[1:])
        except ValueError:
            print("security-fw.sh: %s:%d is not valid Intel HEX" % (src, lineno), file=sys.stderr)
            sys.exit(1)
        n, typ, data = b[0], b[3], b[4:-1]
        if (sum(b) & 0xFF) != 0:
            print("security-fw.sh: %s:%d checksum mismatch" % (src, lineno), file=sys.stderr)
            sys.exit(1)
        if typ == 0x00:
            chunks.append(data[:n])
        elif typ == 0x04:
            base = int.from_bytes(data, "big") << 16
        elif typ == 0x02:
            base = int.from_bytes(data, "big") << 4
        elif typ == 0x01:
            break
with open(dst, "wb") as fh:
    fh.write(b"".join(chunks))
PY
	;;
*)
	cp "$IMAGE" "$RAW"
	;;
esac

SZ="$(wc -c <"$RAW" | tr -d ' ')"
printf '\n%sfirmware audit%s  %s%s — %s bytes of image%s\n' \
	"$BOLD" "$RESET" "$DIM" "$IMAGE" "$SZ" "$RESET"

# ---- keys ------------------------------------------------------------------
# The denylist is byte patterns, not strings, because the thing being looked for is key material:
# a 32-byte URSK that leaked out of a test fixture into a production image does not appear in
# `strings` output and never will.
gate_keys() {
	hdr "fw keys · material that must not ship"
	if [ ! -f "$DENYLIST" ]; then
		printf '  %s%s%s no denylist at %s\n' "$RED" "$CRS" "$RESET" "$DENYLIST"
		printf '      An empty denylist is not a clean result. Seed it from the test vectors:\n'
		printf '      every fixed key in tests/host/ is a value that must never reach an image.\n'
		return 1
	fi
	FW_RAW="$RAW" FW_DENY="$DENYLIST" python3 - <<'PY'
import os, sys

raw = open(os.environ["FW_RAW"], "rb").read()
pats, bad = [], []
for lineno, ln in enumerate(open(os.environ["FW_DENY"]), 1):
    ln = ln.split("#")[0].strip()
    if not ln:
        continue
    label, _, hexs = ln.partition("=")
    hexs = (hexs or label).strip().replace(" ", "")
    label = label.strip() if _ else "pattern"
    try:
        pats.append((label, bytes.fromhex(hexs)))
    except ValueError:
        bad.append((lineno, ln))

print("  scope: %d pattern(s), %d bytes of image" % (len(pats), len(raw)))
for lineno, ln in bad:
    print("  BLOCK %s:%d is not hex: %s" % (os.environ["FW_DENY"], lineno, ln))

hits = []
for label, p in pats:
    # Short patterns match by chance in a megabyte of firmware and would train people to ignore
    # this check, so the denylist format requires enough bytes to be meaningful.
    if len(p) < 8:
        print("  BLOCK pattern '%s' is %d bytes; under 8 it collides by chance" % (label, len(p)))
        bad.append((0, label))
        continue
    off = raw.find(p)
    if off >= 0:
        hits.append((label, off, len(p)))

for label, off, n in hits:
    print("  BLOCK %s present in the image at offset 0x%X (%d bytes)" % (label, off, n))
    print("        A value from the test fixtures reached a shipped artifact. Find the #ifdef")
    print("        or default that let it through; do not just remove it from the denylist.")
sys.exit(1 if (hits or bad) else 0)
PY
}

# ---- strings ---------------------------------------------------------------
# Build-host paths in an image are two problems wearing one coat: they name the machine and the
# user that built it, and they mean the artifact is not reproducible, so nobody can independently
# rebuild a release and compare. Zephyr fixes both with -ffile-prefix-map; this notices when that
# is not in effect.
gate_strings() {
	hdr "fw strings · build-host leakage"
	FW_RAW="$RAW" python3 - <<'PY'
import os, re, sys

raw = open(os.environ["FW_RAW"], "rb").read()
strs = re.findall(rb"[\x20-\x7e]{6,}", raw)
text = b"\n".join(strs).decode("ascii", "replace")

RULES = [
    (r"/Users/[A-Za-z0-9._-]+", "an absolute macOS home path names the machine and the user that "
                                "built this, and means the artifact is not reproducible"),
    (r"/home/[A-Za-z0-9._-]+", "an absolute Linux home path, same problem"),
    (r"/Volumes/[A-Za-z0-9 ._-]+", "an absolute mount path from the build host"),
    (r"[A-Za-z]:\\\\Users\\\\[A-Za-z0-9._-]+", "an absolute Windows user path"),
]
ADVISORY = [
    (r"\b(TODO|FIXME|XXX)\b", "developer note compiled into the image"),
    (r"-----BEGIN [A-Z ]+-----", "a PEM header in a firmware image"),
]

block, warn = [], []
for pat, why in RULES:
    for m in sorted(set(re.findall(pat, text)))[:6]:
        block.append((m, why))
for pat, why in ADVISORY:
    hits = sorted(set(re.findall(pat, text)))
    if hits:
        warn.append((", ".join(hits[:4]), why))

print("  scope: %d printable string(s)" % len(strs))
for what, why in block:
    print("  BLOCK %s" % what)
    print("        %s" % why)
    print("        Fix at the build, not the artifact: -ffile-prefix-map=$(pwd)=. in the")
    print("        toolchain flags removes the whole class.")
for what, why in warn:
    print("  warn  %s — %s" % (what, why))
print("  %d blocking, %d advisory" % (len(block), len(warn)))
sys.exit(1 if block else 0)
PY
}

# ---- dwarf -----------------------------------------------------------------
# Advisory, not blocking. Debug information in a shipped image is a reverse-engineering
# convenience rather than a vulnerability, and this project publishes its source anyway — so the
# argument for stripping is size, not secrecy, and a hard failure would be theatre.
gate_dwarf() {
	hdr "fw dwarf · debug information in a release image"
	local elf="${IMAGE%.hex}.elf"
	[ -f "$elf" ] || elf="build/zephyr/zephyr.elf"
	if [ ! -f "$elf" ]; then
		printf '  %sno ELF beside the image; nothing to inspect%s\n' "$DIM" "$RESET"
		return 0
	fi
	if ! have readelf && ! have llvm-readelf; then
		printf '  %sreadelf not available; skipped (advisory check only)%s\n' "$DIM" "$RESET"
		return 0
	fi
	local re="readelf"
	have readelf || re="llvm-readelf"
	local n
	n="$("$re" -S "$elf" 2>/dev/null | grep -c '\.debug_' || true)"
	printf '  %sscope: %s%s\n' "$DIM" "$elf" "$RESET"
	if [ "${n:-0}" -gt 0 ]; then
		printf '  %s%s%s %s .debug_* section(s) present\n' "$YEL" "$WRN" "$RESET" "$n"
		printf '      Advisory: the source is public, so this costs flash rather than secrecy.\n'
	else
		printf '  %s%s%s no debug sections\n' "$GRN" "$CHK" "$RESET"
	fi
	return 0
}

# ---- size ------------------------------------------------------------------
# Same reasoning as the oversized-file rule in security-diff.sh, applied to the artifact: large
# additions are where payloads hide, because nobody reads to the end of them. A 40 KB jump in an
# image nobody looked at is exactly as invisible as a 40 KB blob nobody read.
gate_size() {
	hdr "fw size · growth against the recorded baseline"
	local key
	key="$(basename "$IMAGE")"
	if [ -n "${FW_UPDATE_BASELINE:-}" ]; then
		mkdir -p "$(dirname "$BASELINE")"
		grep -v "^$key " "$BASELINE" 2>/dev/null >"$BASELINE.tmp" || true
		printf '%s %s\n' "$key" "$SZ" >>"$BASELINE.tmp"
		mv -f "$BASELINE.tmp" "$BASELINE"
		printf '  recorded %s = %s bytes\n' "$key" "$SZ"
		return 0
	fi
	local prev
	prev="$(awk -v k="$key" '$1==k {print $2}' "$BASELINE" 2>/dev/null | tail -1)"
	if [ -z "$prev" ]; then
		printf '  %s%s%s no baseline for %s\n' "$YEL" "$WRN" "$RESET" "$key"
		printf '      Record one: FW_UPDATE_BASELINE=1 scripts/security-fw.sh size\n'
		return 0
	fi
	local delta pct
	delta=$((SZ - prev))
	pct=$(((delta * 100) / prev))
	printf '  %s: %s -> %s bytes (%s%%)\n' "$key" "$prev" "$SZ" "$pct"
	if [ "$pct" -ge "$SIZE_FAIL" ]; then
		printf '  %s%s%s image grew %s%% against the baseline (fail at %s%%)\n' \
			"$RED" "$CRS" "$RESET" "$pct" "$SIZE_FAIL"
		printf '      Justify it in the pull request and update the baseline in the same commit.\n'
		return 1
	fi
	if [ "$pct" -ge "$SIZE_WARN" ]; then
		printf '  %s%s%s grew %s%% (warn at %s%%)\n' "$YEL" "$WRN" "$RESET" "$pct" "$SIZE_WARN"
	fi
	return 0
}

# ---- dispatch --------------------------------------------------------------
run_one() {
	case "$1" in
	keys) gate_keys ;;
	strings) gate_strings ;;
	dwarf) gate_dwarf ;;
	size) gate_size ;;
	*)
		echo "security-fw.sh: unknown check '$1' (keys strings dwarf size)" >&2
		return 2
		;;
	esac
}

CHECKS=("$@")
[ ${#CHECKS[@]} -gt 0 ] || CHECKS=(keys strings dwarf size)

failed=()
for c in "${CHECKS[@]}"; do
	run_one "$c" || failed+=("$c")
done

printf '\n'
if [ ${#failed[@]} -gt 0 ]; then
	printf '%s%s fw: %s%s\n\n' "$RED" "$CRS" "${failed[*]}" "$RESET"
	exit 1
fi
printf '%s%s fw: %s%s\n\n' "$GRN" "$CHK" "${CHECKS[*]}" "$RESET"
