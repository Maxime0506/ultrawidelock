#!/usr/bin/env bash
#
# sdk-export.sh — stage the installable SDK package into a versioned tarball.
#
#   scripts/sdk-export.sh          ->  build/sdk-export/ultrawidelock-sdk-<version>.tar.gz
#
# The tarball is the hardware-agnostic subset: the public headers, the portable
# static library, the CMake package, and the license texts. No vendored code and
# no port trees are inside it; tests/sdk/run.sh is the gate that keeps the
# installed surface honest, this script only wraps that surface for shipping.
#
# Exit 0 on a complete tarball, 1 on any failure. A tarball missing its license
# texts is a failure: two exported seam headers are ISC-scoped with no inline
# notice, so the license file must travel in every copy.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

die() {
	printf '\n  sdk-export: %s\n' "$1" >&2
	shift
	for line in "$@"; do printf '  %s\n' "$line" >&2; done
	printf '\n' >&2
	exit 1
}

find_cmake() {
	if [ -n "${CMAKE:-}" ]; then
		printf '%s\n' "$CMAKE"
		return
	fi
	if command -v cmake >/dev/null 2>&1; then
		command -v cmake
		return
	fi

	local candidate
	for candidate in "${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/cmake/*/bin/cmake \
		"${IDF_TOOLS_PATH:-$HOME/.espressif}"/tools/cmake/*/CMake.app/Contents/bin/cmake; do
		if [ -x "$candidate" ]; then
			printf '%s\n' "$candidate"
			return
		fi
	done
	return 1
}

CMAKE_BIN="$(find_cmake)" || die "cmake not found" "set CMAKE=/path/to/cmake"

SDK_VERSION="$(sed -n '1p' "$ROOT/VERSION")"
printf '%s\n' "$SDK_VERSION" | grep -Eq '^[0-9]+[.][0-9]+[.][0-9]+$' ||
	die "VERSION is not major.minor.patch"

STAGE="${ALIRO_BUILD_ROOT:-$ROOT/build}/sdk-export"
NAME="ultrawidelock-sdk-$SDK_VERSION"

rm -rf "$STAGE"
mkdir -p "$STAGE"

"$CMAKE_BIN" -G "Unix Makefiles" -S "$ROOT" -B "$STAGE/build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$STAGE/$NAME" >/dev/null
MAKEFLAGS= MFLAGS= "$CMAKE_BIN" --build "$STAGE/build" >/dev/null
"$CMAKE_BIN" --install "$STAGE/build" >/dev/null

for f in LICENSE THIRD_PARTY_NOTICES modules/ultrawidelock_dw3000/LICENSE.txt; do
	[ -f "$STAGE/$NAME/share/doc/ultrawidelock/$f" ] ||
		die "license text missing from the staged tree: $f" \
			"the install rules in CMakeLists.txt must place it; do not ship without it"
done

tar -C "$STAGE" -czf "$STAGE/$NAME.tar.gz" "$NAME"

(
	cd "$STAGE"
	export LC_ALL=C
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$NAME.tar.gz"
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$NAME.tar.gz"
	else
		die "no sha256sum and no shasum" "one of them is needed to checksum the tarball"
	fi
) >"$STAGE/$NAME.tar.gz.sha256"

printf '\n  sdk tarball ready  ·  %s\n\n' "$STAGE/$NAME.tar.gz"
tar -tzf "$STAGE/$NAME.tar.gz" | sed 's/^/    /'
printf '\n'
