#!/usr/bin/env bash
# Verify the pinned Mbed TLS tree still supports the crypto backend selected in
# ports/freertos-nrf52833/platform.lock.yml.
#
# The host suite compiles the port's crypto sources against doubles, which
# proves the logic and proves nothing about the library. Everything checked
# here is a contract the port depends on and cannot see: the exact spelling of
# a header Mbed TLS includes for us, the signature of a function it calls by
# name, and the PSA_WANT_* options that decide whether the algorithms this
# product uses are actually built.
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'usage: %s <ncs-workspace>\n' "$0" >&2
	exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd)
lock="$root/ports/freertos-nrf52833/platform.lock.yml"
crypto="$root/ports/freertos-nrf52833/crypto"
mbedtls="$1/modules/crypto/mbedtls"

lock_value() {
	awk -v section="$1" -v key="$2" '
		$0 == section ":" { active = 1; next }
		active && $0 !~ /^ / { exit }
		active && $1 == key ":" { print $2; exit }
	' "$lock"
}

fail() {
	printf 'crypto-source-check: %s\n' "$1" >&2
	exit 2
}

if ! git -C "$mbedtls" rev-parse --git-dir >/dev/null 2>&1; then
	fail 'Mbed TLS checkout is missing'
fi
expected=$(lock_value mbedtls revision)
actual=$(git -C "$mbedtls" rev-parse HEAD)
if [ "$actual" != "$expected" ]; then
	fail 'Mbed TLS revision does not match platform.lock.yml'
fi
printf '  ok   mbedtls revision matches platform.lock.yml\n'

# The version the config file was written against. A major or minor bump moves
# the PSA_WANT_* set and the legacy-from-PSA derivation, both of which the
# config depends on being what they are.
version=$(lock_value mbedtls version)
major=${version%%.*}
minor=${version#*.}
minor=${minor%%.*}
if ! rg -q "#define MBEDTLS_VERSION_MAJOR[[:space:]]+${major}\b" \
	"$mbedtls/include/mbedtls/build_info.h" ||
	! rg -q "#define MBEDTLS_VERSION_MINOR[[:space:]]+${minor}\b" \
		"$mbedtls/include/mbedtls/build_info.h"; then
	fail "Mbed TLS is not the ${major}.${minor} series the config file targets"
fi
printf '  ok   Mbed TLS is the %s.%s series the config file targets\n' "$major" "$minor"

# The build is standalone: its own CMake, no Zephyr. If this file ever became a
# Zephyr shim the whole backend decision would need revisiting, because the
# reason Mbed TLS was chosen over nrf_security is that it builds without one.
if [ ! -f "$mbedtls/CMakeLists.txt" ]; then
	fail 'Mbed TLS has no standalone CMakeLists.txt'
fi
if rg -Fq 'zephyr_' "$mbedtls/CMakeLists.txt"; then
	fail 'Mbed TLS top-level CMake now depends on Zephyr'
fi
printf '  ok   Mbed TLS still builds standalone\n'

# crypto/threading_alt.h exists under that exact name because threading.h
# includes it by that exact name. Nothing else in this repository would notice
# a rename; the first target link would.
if ! rg -Fq '#include "threading_alt.h"' "$mbedtls/include/mbedtls/threading.h"; then
	fail 'Mbed TLS no longer includes threading_alt.h by that name'
fi
for symbol in mbedtls_threading_set_alt; do
	if ! rg -q "[[:space:]]${symbol}\(" "$mbedtls/include/mbedtls/threading.h"; then
		fail "Mbed TLS threading API is missing: $symbol"
	fi
done
if ! rg -Fq 'MBEDTLS_ERR_THREADING_MUTEX_ERROR' "$mbedtls/include/mbedtls/threading.h"; then
	fail 'Mbed TLS threading error code changed'
fi
printf '  ok   Mbed TLS threading-alt contract matches the port\n'

# mbedtls_hardware_poll is called by name and never declared by the port, so a
# changed signature would compile on both sides and link to nothing. The fake
# under tests/ports/freertos-nrf52833/fake/library/ is a copy of this line, and
# this is what keeps the copy honest.
poll_decl="$mbedtls/include/library/entropy_poll.h"
if [ ! -f "$poll_decl" ]; then
	fail 'Mbed TLS entropy_poll.h has moved'
fi
if ! rg -Uq 'int mbedtls_hardware_poll\(void \*data,\s*\n?\s*unsigned char \*output, size_t len, size_t \*olen\);' \
	"$poll_decl"; then
	fail 'mbedtls_hardware_poll signature changed'
fi
if ! rg -Fq 'MBEDTLS_ERR_ENTROPY_SOURCE_FAILED' "$mbedtls/include/mbedtls/entropy.h"; then
	fail 'Mbed TLS entropy error code changed'
fi
printf '  ok   mbedtls_hardware_poll signature matches the port copy\n'

# The allocator macros. If MBEDTLS_PLATFORM_MEMORY stopped honouring the macro
# form, the library would fall back to newlib's calloc, which this image has no
# heap behind.
if ! rg -Fq '#define mbedtls_calloc     MBEDTLS_PLATFORM_CALLOC_MACRO' \
	"$mbedtls/include/mbedtls/platform.h"; then
	fail 'Mbed TLS platform allocator macro form changed'
fi
printf '  ok   Mbed TLS honours the compile-time allocator macros\n'

# Every PSA_WANT_* the port's config sets has to be an option the library
# actually recognises. A typo here is silent: the option is simply never seen,
# the algorithm is not built, and the first failure is a runtime
# PSA_ERROR_NOT_SUPPORTED on the board.
missing=0
while read -r want; do
	[ -n "$want" ] || continue
	if ! rg -Fq "$want" "$mbedtls/include/psa/crypto_config.h"; then
		printf 'crypto-source-check: PSA option is not recognised by Mbed TLS: %s\n' \
			"$want" >&2
		missing=1
	fi
done <<EOF
$(rg -o '^#define (PSA_WANT_[A-Z0-9_]+)' -r '$1' "$crypto/psa_crypto_config_freertos.h")
EOF
if [ "$missing" -ne 0 ]; then
	exit 2
fi
printf '  ok   every PSA_WANT option the port sets is recognised\n'

# The config drives the legacy modules from the PSA list rather than naming
# them, which only works while this derivation header exists.
if [ ! -f "$mbedtls/include/mbedtls/config_adjust_legacy_from_psa.h" ]; then
	fail 'Mbed TLS no longer derives the legacy module set from PSA_WANT'
fi
printf '  ok   Mbed TLS still derives legacy modules from the PSA list\n'

# OpenThread stays on its upstream default rather than PSA, which is what keeps
# Zephyr's crypto_psa.c and the whole persistent-key path out of this build.
# Assert the default is still what the port assumes it is.
ot_config="$1/modules/lib/openthread/src/core/config/crypto.h"
if [ ! -f "$ot_config" ]; then
	fail 'OpenThread crypto configuration header has moved'
fi
if ! rg -Uq '#define OPENTHREAD_CONFIG_CRYPTO_LIB[[:space:]]+OPENTHREAD_CONFIG_CRYPTO_LIB_MBEDTLS' \
	"$ot_config"; then
	fail 'OpenThread no longer defaults to the Mbed TLS crypto library'
fi
printf '  ok   OpenThread still defaults to Mbed TLS rather than PSA\n'
