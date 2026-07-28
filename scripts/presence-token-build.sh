#!/bin/sh
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
source_dir="$repo_root/host/macos-ctk"
build_dir="${PRESENCE_TOKEN_BUILD_DIR:-$repo_root/build/presence-token-xcode}"
output="${1:-$repo_root/build/openaliro-presence-token.zip}"
app="$build_dir/Release/OpenAliro Presence Token.app"
extension="$app/Contents/PlugIns/PresenceTokenExtension.appex"

for tool in cmake xcodebuild swiftc codesign ditto; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'presence-token: required tool not found: %s\n' "$tool" >&2
        exit 1
    }
done

case "$(uname -s)" in
    Darwin) ;;
    *)
        printf 'presence-token: the CryptoTokenKit bundle must be built on macOS\n' >&2
        exit 1
        ;;
esac

mkdir -p "$build_dir" "$(dirname -- "$output")"
mkdir -p "$build_dir/swift-module-cache"
swiftc -swift-version 5 \
    -module-cache-path "$build_dir/swift-module-cache" \
    "$source_dir/Extension/PIVCodec.swift" \
    "$source_dir/Tests/PIVCodecTests.swift" \
    -o "$build_dir/piv-codec-test"
"$build_dir/piv-codec-test"

cmake -S "$source_dir" -B "$build_dir" -G Xcode
cmake --build "$build_dir" --config Release --target PresenceTokenHost

test -x "$extension/Contents/MacOS/PresenceTokenExtension"
test -x "$app/Contents/MacOS/OpenAliro Presence Token"

codesign --force --sign - \
    --entitlements "$source_dir/Extension/PresenceTokenExtension.entitlements" \
    "$extension"
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"

stage=$(mktemp -d "${TMPDIR:-/tmp}/openaliro-presence-token.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
mkdir -p "$stage/openaliro-presence-token"
ditto "$app" "$stage/openaliro-presence-token/OpenAliro Presence Token.app"
cp "$source_dir/VM-INSTALL.md" "$stage/openaliro-presence-token/VM-INSTALL.md"

temporary="$stage/$(basename -- "$output")"
ditto -c -k --sequesterRsrc \
    "$stage/openaliro-presence-token" "$temporary"
mv -f "$temporary" "$output"

digest=$(shasum -a 256 "$output" | awk '{print $1}')
size=$(stat -f '%z' "$output")
printf 'presence-token: built %s (%s bytes, sha256 %s)\n' \
    "$output" "$size" "$digest"
