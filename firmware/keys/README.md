# The MCUboot image-signing key

One file lives here, `mcuboot_ec_p256.pem`, and it is gitignored. It is an
ECDSA P-256 private key. MCUboot embeds its public half at build time and
refuses to boot any image that half did not sign.

```bash
make dfu-key      # generate it, once per clone. Never overwrites.
```

**One key, both Zephyr ports.** The DWM3001CDK signs every image with it, and so
does the nRF5340 DK under its default `DFU=1`. The question it answers is "what
firmware will a board of mine boot", and that question has one answer per
checkout. The path lives in the top-level `Makefile` as `SIGN_KEY`; it sits under
`firmware/` for a dull reason rather than a principled one, which is that moving
it would be a key rotation for every existing checkout. If you deploy these
boards, separate per-product keys are the right shape and this is not that.

The nRF5340 DK's `DFU=0` bench layout has no bootloader and needs no key.

## Why this exists instead of the default

MCUboot ships `root-ec-p256.pem` in its own repository and uses it when nothing
else is configured. That key is public. Firmware signed with it is accepted by
every stock MCUboot in the world, so on a lock it is not a signing key at all,
it is a formality.

MCUboot does notice, at `boot/zephyr/CMakeLists.txt:449-452`, and emits a
`message(WARNING ...)`. A warning inside a ten-thousand-line build log is how
that survived on both boards for as long as it did. It is now fatal.

[`scripts/check-signing-key.sh`](../../scripts/check-signing-key.sh) holds
MCUboot's full list of seven default key files and the four refusals: nothing
configured, a demo basename, a relative path, and a path that does not exist. It
is one file because both ports run it, and they enforce it differently only
because they have to:

| board | where the check runs | what it reads |
|---|---|---|
| DWM3001CDK | `firmware/sysbuild.cmake`, at configure time | `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` |
| nRF5340 DK | `scripts/build-nrf5340dk.sh`, before and after the build | the flag going in, then `CONFIG_BOOT_SIGNATURE_KEY_FILE` in the built `mcuboot/zephyr/.config` |

The DK cannot have a `sysbuild.cmake` of its own: its application is a fetched
upstream tree this repo never edits. Its post-build readback is the stronger half
anyway, because it reports what the bootloader was actually compiled with rather
than what the build was asked for.

`--self-test` on that script proves the refusals still fire, including that it
still accepts a valid key. A gate that refuses everything has stopped being a
gate.

## Two traps worth knowing before you change the wiring

**The path must be absolute.** Sysbuild passes this symbol to the bootloader
image through `set_config_string()`
(`zephyr/share/sysbuild/images/bootloader/CMakeLists.txt:17`), never through a
`.conf` file. MCUboot's own resolver searches the text of its conf files for the
path string to work out a base directory
(`boot/zephyr/CMakeLists.txt:410-420`), finds nothing, and so a relative path
falls through to line 428: `${MCUBOOT_DIR}/<path>`. It resolves *inside the
MCUboot repository*, which is exactly how a wrong path turns back into the demo
key without saying so.

**The quotes are part of the value.** `zephyr/cmake/modules/kconfig.cmake:264`
writes a command-line cache variable through verbatim, so `mk/cdk.mk` passes
`-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE='"/abs/path.pem"'` and
`scripts/build-nrf5340dk.sh` passes the same shape. Drop the inner quotes and you
get invalid Kconfig string syntax.

## What losing it costs, and what rotating it costs

Losing the key costs you nothing on a board you can still reach with a debugger:
`make dfu-key` makes a new one and `make flash` writes a matching MCUboot and
application together. It costs you the board only if serial recovery is the
only way in, because then nothing you can sign will be accepted.

Rotating it has the same shape. MCUboot lives at `0x00000` and is rewritten by
`make flash`, so a full flash moves both halves at once. What you must not do is
sign an update with a new key and push it over serial recovery to a board still
running an MCUboot that holds the old public half: it will refuse the image, and
with `CONFIG_SINGLE_APPLICATION_SLOT=y` there is no second slot holding the
previous one.

`settings_storage` at `0x7e000` is untouched by any of this. The Matter fabrics,
the trust anchors and the reader identity survive a key rotation, because
`make flash` never passes `--erase`.

The two paragraphs above are the DWM3001CDK's shape. The nRF5340 DK is easier on
both counts: its secondary slot lives on the external QSPI, so a rotation there
has a slot to fall back to, and `make nrf-flash` rewrites MCUboot and the
application together over the probe.

## CI

CI generates a throwaway key with `make dfu-key` before it builds the CDK, and
that is deliberate rather than a shortcut. CI artifacts are build-verification
only. If CI signed with the real key, the real key would have to live in the CI
provider's secret store, and an image anyone could download would be one a
deployed lock accepts.

The nRF5340 DK jobs need no key at all, because they call
`scripts/build-nrf5340dk.sh` directly rather than through make, and `DFU`
defaults to off there. `make nrf-build` is where `DFU=1` becomes the default, so
that is where the key becomes a prerequisite.

There is no key escrow here and no key in any secret store. If you deploy these
boards for real, the private key belongs wherever your other production secrets
live, and this README is not that place.
