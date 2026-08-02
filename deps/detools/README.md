# detools, C applier only

Vendored from detools 0.52.0, BSD 2-Clause (see `LICENSE`). Upstream:
<https://github.com/eerimoq/detools>.

Only the embedded applier is here: `c/detools.c`, `c/detools.h`, and the
heatshrink decoder it needs. The Python half that *creates* patches is not
vendored; the host tooling calls the `detools` pip package instead, so the
version that writes a patch and the version that applies it are pinned
separately. `scripts/` records the pin.

These files are copied, not modified. If they ever need a change, record it
here, because nothing else in the build will make a local edit visible.

## Where it came from

The same applier was already in this tree, under
`ports/esp32/apps/matter-lock/managed_components/espressif__esp_delta_ota/`.
That copy arrives through the ESP-IDF component manager and belongs to the
ESP32 port, so it is fetched rather than tracked, and it is not on any include
path an nRF build can reach. This copy exists so the DWM3001CDK bootloader can
link it. Do not delete one assuming the other covers it.

## What uses it

`modules/woz_dfu`, compiled into the **MCUboot image** for the DWM3001CDK.
The application cannot apply the patch itself, because it would be rewriting
the flash it is executing from. See `internal/cdk-ble-ota-plan.md`.

## Build configuration

`modules/woz_dfu/CMakeLists.txt` compiles it with `DETOOLS_CONFIG_FILE_IO=0`
and only the `none` and `heatshrink` decompressors. LZMA is the expensive one
and is switched off; a patch created with `-c lzma` will therefore fail to
apply, deliberately and at the first byte rather than halfway through.

Heatshrink's window and lookahead are carried in the first byte of the
compressed stream, but with `HEATSHRINK_DYNAMIC_ALLOC 0` the decoder is a static
struct fixed at compile time, so the transmitted values have to match
`c/heatshrink/heatshrink_config.h`: **window 8 bits, lookahead 7**.

They are checked, not assumed. `detools.c:288-291` compares the stream header
against `HEATSHRINK_STATIC_WINDOW_BITS` and `HEATSHRINK_STATIC_LOOKAHEAD_BITS`
and returns `-DETOOLS_HEATSHRINK_HEADER` on a mismatch, so a patch built with
the wrong parameters fails at its first byte rather than decoding to garbage.
The host tooling passes these explicitly for that reason.
